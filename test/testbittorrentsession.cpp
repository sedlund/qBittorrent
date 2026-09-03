/*
 * Bittorrent Client using Qt and libtorrent.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give permission to
 * link this program with the OpenSSL project's "OpenSSL" library (or with
 * modified versions of it that use the same license as the "OpenSSL" library),
 * and distribute the linked executables. You must obey the GNU General Public
 * License in all respects for all of the code used other than "OpenSSL". If you
 * modify file(s), you may extend this exception to your version of the file(s),
 * but you are not obligated to do so. If you do not wish to do so, delete this
 * exception statement from your version.
 */

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include <libtorrent/create_torrent.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/hasher.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/version.hpp>

#include <QObject>
#include <QDir>
#include <QFileInfo>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

#include "base/bittorrent/addtorrentparams.h"
#include "base/bittorrent/downloadpriority.h"
#include "base/bittorrent/peeraddress.h"
#include "base/bittorrent/session.h"
#include "base/bittorrent/torrentdescriptor.h"
#include "base/bittorrent/torrentimpl.h"
#include "base/logger.h"
#include "base/net/downloadmanager.h"
#include "base/net/proxyconfigurationmanager.h"
#include "base/path.h"
#include "base/preferences.h"
#include "base/profile.h"
#include "base/settingsstorage.h"

namespace
{
    constexpr int PIECE_SIZE = 16 * 1024;

    struct TestTorrent
    {
        BitTorrent::TorrentDescriptor descriptor;
    };

    struct SharedPieceTorrent
    {
        BitTorrent::TorrentDescriptor descriptor;
        std::vector<std::pair<std::string, std::string>> files;
    };

    TestTorrent createTestTorrent(const int id, const bool useScreenshotsDirectory = false)
    {
        lt::file_storage files;
        const std::string root = "test-" + std::to_string(id) + "/";
        files.add_file(root + "wanted.bin", PIECE_SIZE);
        files.add_file(root + (useScreenshotsDirectory ? "screenshots/screen.png" : "screen.png"), PIECE_SIZE);

        lt::create_torrent torrent {files, PIECE_SIZE};
        for (int index = 0; index < 2; ++index)
        {
            const QByteArray piece(PIECE_SIZE, static_cast<char>(id + index));
            lt::hasher hasher;
            hasher.update(piece.constData(), piece.size());
            torrent.set_hash(lt::piece_index_t {index}, hasher.final());
        }

        std::vector<char> data;
        lt::bencode(std::back_inserter(data), torrent.generate());
        const auto descriptor = BitTorrent::TorrentDescriptor::load(QByteArray(data.data(), data.size()));
        if (!descriptor)
            qFatal("Failed to load test torrent: %s", qPrintable(descriptor.error()));

        return {.descriptor = descriptor.value()};
    }

    SharedPieceTorrent createSharedPieceTorrent(const int id)
    {
        lt::file_storage files;
        const std::string root = "shared-" + std::to_string(id) + "/";
        files.add_file(root + "wanted.bin", PIECE_SIZE / 2);
        files.add_file(root + "screenshots/one.png", PIECE_SIZE / 2);
        files.add_file(root + "screenshots/two.png", PIECE_SIZE);

#if LIBTORRENT_VERSION_NUM >= 20000
        lt::create_torrent torrent {files, PIECE_SIZE, lt::create_torrent::v1_only};
#else
        lt::create_torrent torrent {files, PIECE_SIZE};
#endif
        std::vector<std::pair<std::string, std::string>> content {
            {root + "wanted.bin", std::string(PIECE_SIZE / 2, 'a')},
            {root + "screenshots/one.png", std::string(PIECE_SIZE / 2, 'b')},
            {root + "screenshots/two.png", std::string(PIECE_SIZE, 'c')}
        };
        const std::array<std::string, 2> pieces {content[0].second + content[1].second, content[2].second};
        for (int index = 0; index < 2; ++index)
        {
            const std::string &piece = pieces[index];
            lt::hasher hasher;
            hasher.update(piece.data(), static_cast<int>(piece.size()));
            torrent.set_hash(lt::piece_index_t {index}, hasher.final());
        }

        std::vector<char> data;
        lt::bencode(std::back_inserter(data), torrent.generate());
        const auto descriptor = BitTorrent::TorrentDescriptor::load(QByteArray(data.data(), data.size()));
        if (!descriptor)
            qFatal("Failed to load shared-piece test torrent: %s", qPrintable(descriptor.error()));

        return {.descriptor = descriptor.value(), .files = std::move(content)};
    }

    lt::settings_pack createSeederSettings()
    {
        lt::settings_pack settings;
        settings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
        settings.set_bool(lt::settings_pack::enable_dht, false);
        settings.set_bool(lt::settings_pack::enable_lsd, false);
        settings.set_bool(lt::settings_pack::enable_upnp, false);
        settings.set_bool(lt::settings_pack::enable_natpmp, false);
        return settings;
    }
}

class TestBitTorrentSession final : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(TestBitTorrentSession)

public:
    TestBitTorrentSession() = default;

private slots:
    void initTestCase()
    {
        QVERIFY(m_profileDir.isValid());

        Logger::initInstance();
        Profile::initInstance(Path(m_profileDir.path()), {}, false);
        SettingsStorage::initInstance();
        Preferences::initInstance();
        Net::ProxyConfigurationManager::initInstance();
        Net::DownloadManager::initInstance();
        BitTorrent::Session::initInstance();

        m_session = BitTorrent::Session::instance();
        QVERIFY(m_session);

        QSignalSpy restoredSpy(m_session, &BitTorrent::Session::restored);
        QTRY_VERIFY_WITH_TIMEOUT(m_session->isRestored() || !restoredSpy.isEmpty(), 10000);

        m_session->setDHTEnabled(false);
        m_session->setLSDEnabled(false);
        m_session->setPeXEnabled(false);
    }

    void cleanupTestCase()
    {
        BitTorrent::Session::freeInstance();
        Net::DownloadManager::freeInstance();
        Net::ProxyConfigurationManager::freeInstance();
        Preferences::freeInstance();
        SettingsStorage::freeInstance();
        Profile::freeInstance();
        Logger::freeInstance();
    }

    void testFinishedWithIgnoredFile()
    {
        testFinishedStateUpdate({BitTorrent::DownloadPriority::Normal, BitTorrent::DownloadPriority::Ignored}, false);
    }

    void testFilenameFilterAppliedWhenAddingTorrent()
    {
        QTemporaryDir contentDir;
        QVERIFY(contentDir.isValid());

        const bool oldExcludedFileNamesEnabled = m_session->isExcludedFileNamesEnabled();
        const QStringList oldExcludedFileNames = m_session->excludedFileNames();
        [[maybe_unused]] const auto settingsGuard = qScopeGuard([this, oldExcludedFileNamesEnabled, oldExcludedFileNames]
        {
            m_session->setExcludedFileNamesEnabled(oldExcludedFileNamesEnabled);
            m_session->setExcludedFileNames(oldExcludedFileNames);
        });
        m_session->setExcludedFileNamesEnabled(true);
        m_session->setExcludedFileNames({QStringLiteral("screenshots")});

        const TestTorrent testTorrent = createTestTorrent(++m_torrentID, true);
        BitTorrent::AddTorrentParams params;
        params.savePath = Path(contentDir.path());
        params.useDownloadPath = false;
        params.useAutoTMM = false;

        BitTorrent::TorrentImpl *torrent = nullptr;
        const auto torrentAddedConnection = connect(m_session, &BitTorrent::Session::torrentAdded, this
            , [&torrent](BitTorrent::Torrent *const addedTorrent)
        {
            torrent = dynamic_cast<BitTorrent::TorrentImpl *>(addedTorrent);
        });

        QVERIFY(m_session->addTorrent(testTorrent.descriptor, params));
        QTRY_VERIFY_WITH_TIMEOUT(torrent, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(torrent->isDownloading(), 10000);
        const PathList filePaths = torrent->filePaths();
        const QList<BitTorrent::DownloadPriority> filePriorities = torrent->filePriorities();
        disconnect(torrentAddedConnection);

        QVERIFY(m_session->removeTorrent(torrent->id(), BitTorrent::TorrentRemoveOption::KeepContent));
        QTRY_VERIFY_WITH_TIMEOUT(m_session->torrentsCount() == 0, 10000);

        QCOMPARE(filePaths.size(), 2);
        QCOMPARE(filePriorities.size(), filePaths.size());
        const int wantedIndex = filePaths.indexOf(Path(QStringLiteral("test-%1/wanted.bin").arg(m_torrentID)));
        const int screenshotsIndex = filePaths.indexOf(Path(QStringLiteral("test-%1/screenshots/screen.png").arg(m_torrentID)));
        QVERIFY(wantedIndex >= 0);
        QVERIFY(screenshotsIndex >= 0);
        QCOMPARE(filePriorities.at(wantedIndex), BitTorrent::DownloadPriority::Normal);
        QCOMPARE(filePriorities.at(screenshotsIndex), BitTorrent::DownloadPriority::Ignored);
    }

    void testSharedPieceWithExcludedFile()
    {
        QTemporaryDir contentDir;
        QVERIFY(contentDir.isValid());

        const bool oldExcludedFileNamesEnabled = m_session->isExcludedFileNamesEnabled();
        const QStringList oldExcludedFileNames = m_session->excludedFileNames();
        [[maybe_unused]] const auto settingsGuard = qScopeGuard([this, oldExcludedFileNamesEnabled, oldExcludedFileNames]
        {
            m_session->setExcludedFileNamesEnabled(oldExcludedFileNamesEnabled);
            m_session->setExcludedFileNames(oldExcludedFileNames);
        });
        m_session->setExcludedFileNamesEnabled(true);
        m_session->setExcludedFileNames({QStringLiteral("screenshots")});

        const SharedPieceTorrent testTorrent = createSharedPieceTorrent(++m_torrentID);
        const std::filesystem::path rootPath = contentDir.path().toStdString();
        const std::filesystem::path seedPath = rootPath / "seed";
        const std::filesystem::path downloadPath = rootPath / "download";
        for (const auto &[relativePath, data] : testTorrent.files)
        {
            const std::filesystem::path filePath = seedPath / relativePath;
            std::filesystem::create_directories(filePath.parent_path());
            std::ofstream file(filePath, std::ios::binary);
            QVERIFY(file);
            file.write(data.data(), static_cast<std::streamsize>(data.size()));
            QVERIFY(file);
        }

        lt::session seedSession {createSeederSettings()};
        lt::add_torrent_params seedParams = testTorrent.descriptor.ltAddTorrentParams();
        seedParams.save_path = seedPath.string();
        seedParams.flags |= lt::torrent_flags::seed_mode;
        seedParams.flags &= ~lt::torrent_flags::paused;
        try
        {
            seedSession.add_torrent(std::move(seedParams));
        }
        catch (const std::exception &error)
        {
            QFAIL(qPrintable(QStringLiteral("Failed to add seeder: %1").arg(QString::fromUtf8(error.what()))));
        }
        QTRY_VERIFY_WITH_TIMEOUT(seedSession.listen_port() != 0, 5000);

        BitTorrent::AddTorrentParams params;
        params.savePath = Path(QString::fromStdString(downloadPath.string()));
        params.useDownloadPath = false;
        params.useAutoTMM = false;
        params.addForced = true;

        BitTorrent::TorrentImpl *torrent = nullptr;
        int finishedCount = 0;
        const auto torrentFinishedConnection = connect(m_session, &BitTorrent::Session::torrentFinished, this
            , [&torrent, &finishedCount](BitTorrent::Torrent *const finishedTorrent)
        {
            if (finishedTorrent == torrent)
                ++finishedCount;
        });
        const auto torrentAddedConnection = connect(m_session, &BitTorrent::Session::torrentAdded, this
            , [&torrent](BitTorrent::Torrent *const addedTorrent)
        {
            torrent = dynamic_cast<BitTorrent::TorrentImpl *>(addedTorrent);
        });

        QVERIFY(m_session->addTorrent(testTorrent.descriptor, params));
        QTRY_VERIFY_WITH_TIMEOUT(torrent, 10000);
        QVERIFY(torrent->connectPeer({QHostAddress::LocalHost, seedSession.listen_port()}));
        QTRY_COMPARE_WITH_TIMEOUT(finishedCount, 1, 10000);
        disconnect(torrentAddedConnection);
        disconnect(torrentFinishedConnection);

        const QDir downloadDir(QString::fromStdString(downloadPath.string()));
        const QString torrentRoot = QStringLiteral("shared-%1").arg(m_torrentID);
        QVERIFY(QFileInfo::exists(downloadDir.filePath(torrentRoot + QStringLiteral("/wanted.bin"))));
        QVERIFY(!QFileInfo::exists(downloadDir.filePath(torrentRoot + QStringLiteral("/screenshots/one.png"))));
        QVERIFY(!QFileInfo::exists(downloadDir.filePath(torrentRoot + QStringLiteral("/screenshots/two.png"))));
        QVERIFY(!downloadDir.entryList({QStringLiteral("*.parts")}
                    , QDir::Files | QDir::Hidden).isEmpty());

        QVERIFY(m_session->removeTorrent(torrent->id(), BitTorrent::TorrentRemoveOption::KeepContent));
        QTRY_VERIFY_WITH_TIMEOUT(m_session->torrentsCount() == 0, 10000);
    }

    void testFinishedAlertBeforeStateUpdate()
    {
        testFinishedStateUpdate({BitTorrent::DownloadPriority::Normal, BitTorrent::DownloadPriority::Normal}, true);
    }

    void testStateUpdateBeforeFinishedAlert()
    {
        testFinishedStateUpdate({BitTorrent::DownloadPriority::Normal, BitTorrent::DownloadPriority::Normal}, false, true);
    }

    void testFinishedRecheckLifecycle()
    {
        QTemporaryDir contentDir;
        QVERIFY(contentDir.isValid());

        const TestTorrent testTorrent = createTestTorrent(++m_torrentID);
        BitTorrent::AddTorrentParams params;
        params.savePath = Path(contentDir.path());
        params.useDownloadPath = false;
        params.useAutoTMM = false;
        params.filePriorities = {BitTorrent::DownloadPriority::Normal, BitTorrent::DownloadPriority::Normal};

        BitTorrent::TorrentImpl *torrent = nullptr;
        int checkedCount = 0;
        const auto torrentCheckedConnection = connect(m_session, &BitTorrent::Session::torrentFinishedChecking, this
            , [&](BitTorrent::Torrent *const checkedTorrent)
        {
            if (checkedTorrent == torrent)
                ++checkedCount;
        });
        const auto torrentAddedConnection = connect(m_session, &BitTorrent::Session::torrentAdded, this
            , [&torrent](BitTorrent::Torrent *const addedTorrent)
        {
            torrent = dynamic_cast<BitTorrent::TorrentImpl *>(addedTorrent);
        });

        QVERIFY(m_session->addTorrent(testTorrent.descriptor, params));
        QTRY_VERIFY_WITH_TIMEOUT(torrent, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(torrent->isDownloading(), 10000);
        disconnect(torrentAddedConnection);
        QTRY_VERIFY_WITH_TIMEOUT(checkedCount > 0, 10000);

        int finishedCount = 0;
        const auto torrentFinishedConnection = connect(m_session, &BitTorrent::Session::torrentFinished, this
            , [torrent, &finishedCount](BitTorrent::Torrent *const finishedTorrent)
        {
            if (finishedTorrent == torrent)
                ++finishedCount;
        });
        const int checkedCountBeforeRecheck = checkedCount;

        const bool recheckTorrentsOnCompletion = Preferences::instance()->recheckTorrentsOnCompletion();
        Preferences::instance()->recheckTorrentsOnCompletion(true);

        // downloading state update -> m_unchecked becomes true
        lt::torrent_status downloadingStatus = torrent->nativeHandle().status();
        downloadingStatus.state = lt::torrent_status::downloading;
        torrent->handleStateUpdate(downloadingStatus);

        // finished state update -> completion handling -> forced recheck
        lt::torrent_status finishedStatus = downloadingStatus;
        finishedStatus.state = lt::torrent_status::finished;
        torrent->handleStateUpdate(finishedStatus);

        QVERIFY(torrent->isChecking());
        QCOMPARE(finishedCount, 0);

        // recheck -> checked alert -> completion handling is available again
        QTRY_COMPARE_WITH_TIMEOUT(checkedCount, checkedCountBeforeRecheck + 1, 10000);
        QCOMPARE(finishedCount, 0);

        // The recheck has completed; disable another recheck for the follow-up
        // alert so this verifies the scheduled guard itself.
        Preferences::instance()->recheckTorrentsOnCompletion(false);

        // The completion handler must be available again after the recheck.
        torrent->handleTorrentFinished();
        QTRY_COMPARE_WITH_TIMEOUT(finishedCount, 1, 10000);
        QTest::qWait(250);
        QCOMPARE(finishedCount, 1);

        Preferences::instance()->recheckTorrentsOnCompletion(recheckTorrentsOnCompletion);
        disconnect(torrentFinishedConnection);
        disconnect(torrentCheckedConnection);

        QVERIFY(m_session->removeTorrent(torrent->id(), BitTorrent::TorrentRemoveOption::KeepContent));
        QTRY_VERIFY_WITH_TIMEOUT(m_session->torrentsCount() == 0, 10000);
    }

    void testFinishedStateUpdateWithoutDownloadingUpdate()
    {
        QTemporaryDir contentDir;
        QVERIFY(contentDir.isValid());

        const TestTorrent testTorrent = createTestTorrent(++m_torrentID);
        BitTorrent::AddTorrentParams params;
        params.savePath = Path(contentDir.path());
        params.useDownloadPath = false;
        params.useAutoTMM = false;
        params.addForced = true;
        params.filePriorities = {BitTorrent::DownloadPriority::Normal, BitTorrent::DownloadPriority::Ignored};

        BitTorrent::TorrentImpl *torrent = nullptr;
        int finishedCount = 0;
        const auto torrentFinishedConnection = connect(m_session, &BitTorrent::Session::torrentFinished, this
            , [&](BitTorrent::Torrent *const finishedTorrent)
        {
            if (finishedTorrent == torrent)
                ++finishedCount;
        });
        const auto torrentAddedConnection = connect(m_session, &BitTorrent::Session::torrentAdded, this
            , [&torrent](BitTorrent::Torrent *const addedTorrent)
        {
            torrent = dynamic_cast<BitTorrent::TorrentImpl *>(addedTorrent);

            // libtorrent: checking_files -> downloading -> finished
            // qBittorrent: cached checking state -> finished snapshot
            // (the intermediate downloading update was not observed).
            QCOMPARE(torrent->nativeHandle().status().state, lt::torrent_status::downloading);
            QVERIFY(torrent->isChecking());

            lt::torrent_status finishedStatus = torrent->nativeHandle().status();
            finishedStatus.state = lt::torrent_status::finished;
            torrent->handleStateUpdate(finishedStatus);
        });

        QVERIFY(m_session->addTorrent(testTorrent.descriptor, params));
        QTRY_VERIFY_WITH_TIMEOUT(torrent, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(finishedCount, 1, 10000);
        QTest::qWait(250);
        QCOMPARE(finishedCount, 1);
        disconnect(torrentAddedConnection);
        disconnect(torrentFinishedConnection);

        QVERIFY(m_session->removeTorrent(torrent->id(), BitTorrent::TorrentRemoveOption::KeepContent));
        QTRY_VERIFY_WITH_TIMEOUT(m_session->torrentsCount() == 0, 10000);
    }

private:
    void testFinishedStateUpdate(const QList<BitTorrent::DownloadPriority> &priorities, const bool handleFinishedFirst
        , const bool handleFinishedAfterStateUpdate = false)
    {
        QTemporaryDir contentDir;
        QVERIFY(contentDir.isValid());

        const TestTorrent testTorrent = createTestTorrent(++m_torrentID);
        BitTorrent::AddTorrentParams params;
        params.savePath = Path(contentDir.path());
        params.useDownloadPath = false;
        params.useAutoTMM = false;
        params.filePriorities = priorities;

        BitTorrent::TorrentImpl *torrent = nullptr;
        const auto torrentAddedConnection = connect(m_session, &BitTorrent::Session::torrentAdded, this
            , [&torrent](BitTorrent::Torrent *const addedTorrent)
        {
            torrent = dynamic_cast<BitTorrent::TorrentImpl *>(addedTorrent);
        });

        QVERIFY(m_session->addTorrent(testTorrent.descriptor, params));
        QTRY_VERIFY_WITH_TIMEOUT(torrent, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(torrent->isDownloading(), 10000);
        disconnect(torrentAddedConnection);

        int finishedCount = 0;
        const auto torrentFinishedConnection = connect(m_session, &BitTorrent::Session::torrentFinished, this
            , [torrent, &finishedCount](BitTorrent::Torrent *const finishedTorrent)
        {
            if (finishedTorrent == torrent)
                ++finishedCount;
        });

        if (handleFinishedFirst)
        {
            // torrent_finished_alert -> completion handling is scheduled
            torrent->handleTorrentFinished();
        }

        // finished state update -> fallback completion handling
        lt::torrent_status finishedStatus = torrent->nativeHandle().status();
        finishedStatus.state = lt::torrent_status::finished;
        torrent->handleStateUpdate(finishedStatus);

        if (handleFinishedAfterStateUpdate)
        {
            // finished state update -> torrent_finished_alert -> already-finished guard
            torrent->handleTorrentFinished();
        }

        QTRY_COMPARE_WITH_TIMEOUT(finishedCount, 1, 10000);
        QTest::qWait(250);
        QCOMPARE(finishedCount, 1);
        disconnect(torrentFinishedConnection);

        QVERIFY(m_session->removeTorrent(torrent->id(), BitTorrent::TorrentRemoveOption::KeepContent));
        QTRY_VERIFY_WITH_TIMEOUT(m_session->torrentsCount() == 0, 10000);
    }

    QTemporaryDir m_profileDir;
    BitTorrent::Session *m_session = nullptr;
    int m_torrentID = 0;
};

QTEST_GUILESS_MAIN(TestBitTorrentSession)
#include "testbittorrentsession.moc"
