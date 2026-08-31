#ifndef OPTIONSDIALOG_H
#define OPTIONSDIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QStackedWidget>
#include <QShowEvent>
#include <QHideEvent>
#include <QMediaDevices>

class RadioState;
class AudioController;
class HardwareController;
class CatServer;
class KPA1500Client;
class DxClusterController;
class AboutPage;
class StationPage;
class AudioInputPage;
class AudioOutputPage;
class RigControlPage;
class ConnectionController;
class CwKeyerPage;
class StraightKeyPage;
class TxMacrosPage;
class KpodPage;
class Kpa1500Page;
class DxClusterPage;

/**
 * @brief Tabbed Options dialog. Left QListWidget drives a QStackedWidget holding the 8 option
 *        pages (About, Audio Input/Output, Rig Control, CW Keyer, KPOD, KPA1500, DX Cluster).
 *        Pages are lazy-constructed on first tab activation via @c ensurePageCreated.
 */
class OptionsDialog : public QDialog {
    Q_OBJECT

public:
    enum Page {
        PageAbout = 0,
        PageStation,
        PageAudioInput,
        PageAudioOutput,
        PageRigControl,
        PageCwKeyer,
    PageStraightKey,
        PageTxMacros,
        PageKpod,
        PageKpa1500,
        PageDxCluster,
        PageCount
    };

    explicit OptionsDialog(RadioState *radioState, ConnectionController *connectionController,
                           AudioController *audioController,
                           HardwareController *hardwareController, CatServer *catServer, KPA1500Client *kpa1500Client,
                           DxClusterController *dxClusterController, QWidget *parent = nullptr);
    ~OptionsDialog();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void setupUi();
    void ensurePageCreated(int index);
    void refreshPage(int index);

    RadioState *m_radioState;
    ConnectionController *m_connectionController;
    AudioController *m_audioController;
    HardwareController *m_hardwareController;
    CatServer *m_catServer;
    KPA1500Client *m_kpa1500Client;
    DxClusterController *m_dxClusterController;
    QListWidget *m_tabList;
    QStackedWidget *m_pageStack;
    QMediaDevices *m_mediaDevices;
    bool m_pageCreated[PageCount] = {};

    // Page widgets
    AboutPage *m_aboutPage = nullptr;
    StationPage *m_stationPage = nullptr;
    AudioInputPage *m_audioInputPage = nullptr;
    AudioOutputPage *m_audioOutputPage = nullptr;
    RigControlPage *m_rigControlPage = nullptr;
    StraightKeyPage *m_straightKeyPage = nullptr;
    CwKeyerPage *m_cwKeyerPage = nullptr;
    TxMacrosPage *m_txMacrosPage = nullptr;
    KpodPage *m_kpodPage = nullptr;
    Kpa1500Page *m_kpa1500Page = nullptr;
    DxClusterPage *m_dxClusterPage = nullptr;
};

#endif // OPTIONSDIALOG_H
