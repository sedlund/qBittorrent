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

#include <iterator>
#include <vector>

#include <libtorrent/create_torrent.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/hasher.hpp>

#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "base/bittorrent/addtorrentparams.h"
#include "base/bittorrent/downloadpriority.h"
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

    TestTorrent createTestTorrent(const int id)
    {
        lt::file_storage files;
        const std::string root = "test-" + std::to_string(id) + "/";
        files.add_file(root + "wanted.bin", PIECE_SIZE);
        files.add_file(root + "screen.png", PIECE_SIZE);

        lt::create_torrent torrent {files, PIECE_SIZE};
        for (int index = 0; index < 2; ++index)
        {
            const QByteArray piece(PIECE_SIZE, static_cast<char>(id + index));
            lt::hasher hasher;
            hasher.update(piece.constData(), piece.size());
            torrent.set_hash(lt::piece_index_t {index}, hasher.final());
        }

        const std::vector<char> data = torrent.generate_buf();
        const auto descriptor = BitTorrent::TorrentDescriptor::load(QByteArray(data.data(), data.size()));
        if (!descriptor)
            qFatal("Failed to load test torrent: %s", qPrintable(descriptor.error()));

        return {.descriptor = descriptor.value()};
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

    void testFinishedWithIgnoredFileInUnwantedFolder()
    {
        const bool oldUnwantedFolderEnabled = m_session->isUnwantedFolderEnabled();
        [[maybe_unused]] const auto settingsGuard = qScopeGuard([this, oldUnwantedFolderEnabled]
        {
            m_session->setUnwantedFolderEnabled(oldUnwantedFolderEnabled);
        });
        m_session->setUnwantedFolderEnabled(true);

        testFinishedStateUpdate({BitTorrent::DownloadPriority::Normal, BitTorrent::DownloadPriority::Ignored}, true);
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
