#include "VideoWidget.h"
#include "Label.h"
#include "RangeSlider.h"
#include "ComfyBgRemover.h"
#include "VideoExporter.h"
#include "FrameExtractor.h"

#include <QDragEnterEvent>
#include <QMimeData>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QDebug>
#include <QDir>
#include <QMenuBar>
#include <QMenu>
#include <QActionGroup>
#include <QPainter>
#include <QSettings>
#include <QFileInfo>
#include <QKeyEvent>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <cmath>
#include <QRegularExpression>
#include <QFileDialog>
#include <qnetworkreply.h>
#include <QWidgetAction>
#include <QLabel>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>

// ─── Constructor ────────────────────────────────────────────────────────────

VideoWidget::VideoWidget(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("VideoConverter");
    QCoreApplication::setOrganizationName("michaelSW");
    QCoreApplication::setOrganizationDomain("uyuni.de");
    QCoreApplication::setApplicationName("VideoConverter");

    // Menu
    QMenuBar *bar = new QMenuBar(this);
    setMenuBar(bar);
    QMenu *menu = new QMenu("Auflösung", bar);
    bar->addMenu(menu);
    QActionGroup *resGroup = new QActionGroup(this);
    resGroup->setExclusive(true);
    QAction *act1024 = menu->addAction("1024 × 1024", this, [this]{ m_resolution = 1024; });
    act1024->setCheckable(true);
    act1024->setChecked(true);
    resGroup->addAction(act1024);
    QAction *act2048 = menu->addAction("2048 × 2048", this, [this]{ m_resolution = 2048; });
    act2048->setCheckable(true);
    resGroup->addAction(act2048);

    QMenu *menuOpen = new QMenu("Öffnen", bar);
    bar->addMenu(menuOpen);
    QAction *actOpen = menuOpen->addAction("Datei öffnen …", this, &VideoWidget::openFile);
    actOpen->setShortcut(QKeySequence::Open);
    menuOpen->addSeparator();
    m_recentMenu   = menuOpen->addMenu("Zuletzt geöffnet");
    connect(m_recentMenu, &QMenu::hovered, this, [this](QAction *act)
            {
                QLabel *preview = m_recentMenu->findChild<QLabel*>("hoverPreview", Qt::FindDirectChildrenOnly);
                if (!preview)
                {
                    preview = new QLabel(m_recentMenu, Qt::ToolTip | Qt::FramelessWindowHint);
                    preview->setObjectName("hoverPreview");
                    preview->setAttribute(Qt::WA_ShowWithoutActivating);
                }
                const QIcon ic = act->icon();
                const QList<QSize> sizes = ic.availableSizes();
                const QPixmap px = sizes.isEmpty() ? QPixmap() : ic.pixmap(sizes.first());
                if (px.isNull())
                {
                    preview->hide();
                    return;
                }
                preview->setPixmap(px);
                preview->resize(px.size());
                preview->move(QCursor::pos() + QPoint(20, 0));
                preview->show();
            });
    connect(m_recentMenu, &QMenu::aboutToHide, this, [this]
            {
                if (auto *p = m_recentMenu->findChild<QLabel*>("hoverPreview", Qt::FindDirectChildrenOnly))
                    p->hide();
            });

    // Eigener Extractor nur fürs erste Frame jeder History-Datei (asynchron, serialisiert)
    m_previewExtractor = new FrameExtractor("ffmpeg", this);
    connect(m_previewExtractor, &FrameExtractor::firstFrameReady,
            this, &VideoWidget::onPreviewReady);
    for (const QString &entry : QSettings().value("history/files").toStringList())
        enqueuePreviewExtraction(entry.split(",").first());

    rebuildRecentMenu();

    m_exportMenu = menuOpen->addMenu("Zuletzt exportiert");
    rebuildExportMenu();

    QMenu *menuSave = new QMenu("Speichern", bar);
    bar->addMenu(menuSave);
    menuSave->addAction("Exportieren …", this, &VideoWidget::exportDialog);
    menuSave->addSeparator();
    QMenu *menuOpenWith = menuOpen->addMenu("Öffnen mit …");
    menuOpenWith->addAction("Explorer/Website",  this, &VideoWidget::openWithExplorer);
    menuOpenWith->addAction("URL",  this, &VideoWidget::openWithUrl);
    menuOpenWith->addAction("XnView",    this, &VideoWidget::openWithXnView);
    menuOpenWith->addAction("FastStone", this, &VideoWidget::openWithFastStone);

    QMenu *menuEdit = new QMenu("Bearbeiten", bar);
    bar->addMenu(menuEdit);
    menuEdit->addAction("Pause / Weiter  [Space]", this, &VideoWidget::togglePause);
    menuEdit->addAction("min/max reduzieren", this, &VideoWidget::reduceMinMax);

    QMenu *menuFx = new QMenu("Effekte", bar);
    bar->addMenu(menuFx);

    QMenu *menuNode = menuFx->addMenu("ComfyUI Node");
    m_nodeGroup = new QActionGroup(this);
    m_nodeGroup->setExclusive(true);
    for (const QString &n : { "BiRefNet_Hugo", "BiRefNetRMBG" })
    {
        QAction *a = menuNode->addAction(n);
        a->setCheckable(true);
        a->setChecked(n == QLatin1String("BiRefNetRMBG"));
        m_nodeGroup->addAction(a);
    }

    QMenu *menuModel = menuFx->addMenu("BiRefNet Modell");
    m_modelGroup = new QActionGroup(this);
    m_modelGroup->setExclusive(true);

    auto rebuildModelMenu = [this, menuModel]()
    {
        for (QAction *a : m_modelGroup->actions())
        {
            menuModel->removeAction(a);
            m_modelGroup->removeAction(a);
            delete a;
        }
        const bool isRMBG = m_nodeGroup->checkedAction() &&
                            m_nodeGroup->checkedAction()->text() == "BiRefNetRMBG";
        const QStringList models = isRMBG
                                       ? QStringList{ "BiRefNet-general", "BiRefNet_512x512", "BiRefNet-HR",
                                                     "BiRefNet-portrait", "BiRefNet-matting", "BiRefNet-HR-matting",
                                                     "BiRefNet_lite", "BiRefNet_lite-2K", "BiRefNet_dynamic",
                                                     "BiRefNet_lite-matting", "BiRefNet_toonout" }
                                       : QStringList{ "ZhengPeng7/BiRefNet", "ZhengPeng7/BiRefNet_HR",
                                                     "ZhengPeng7/BiRefNet-portrait" };
        for (const QString &m : models)
        {
            QAction *a = menuModel->addAction(m);
            a->setCheckable(true);
            m_modelGroup->addAction(a);
        }
        if (!m_modelGroup->actions().isEmpty())
            m_modelGroup->actions().first()->setChecked(true);
    };
    rebuildModelMenu();

    connect(m_nodeGroup, &QActionGroup::triggered, this, [rebuildModelMenu](QAction *)
            {
                rebuildModelMenu();
            });

    menuFx->addSeparator();
    m_actBgRemove = menuFx->addAction("Hintergrund entfernen (ComfyUI)", this, [this]
                                      {
                                          if (m_bgRemover)
                                          {
                                              m_bgRemover->cancel();
                                              m_bgRemover->deleteLater();
                                              m_bgRemover = nullptr;
                                              m_actBgRemove->setText("Hintergrund entfernen (ComfyUI)");
                                              setWindowTitle("VideoConverter");
                                          }
                                          else
                                          {
                                              startBgRemoval();
                                          }
                                      });
    menuFx->addSeparator();
    menuFx->addAction("Hintergrund wiederherstellen", this, [this]
                      {
                          m_bigMap = m_bigMapBackup;
                      });

    // ── Layout ──
    resize(1000, 680);
    QWidget *central = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(2, 2, 2, 2);
    mainLayout->setSpacing(2);

    m_stack = new QStackedWidget(central);

    // Page 0: live video/GIF
    m_label = new Label("Zieh ein Video hierher", m_stack);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_stack->addWidget(m_label);  // index 0

    // Page 1: sprite sheet + animated preview side by side
    QWidget *gridPage = new QWidget(m_stack);
    QHBoxLayout *gridLayout = new QHBoxLayout(gridPage);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    gridLayout->setSpacing(4);

    m_gridLabel = new QLabel(gridPage);
    m_gridLabel->setAlignment(Qt::AlignCenter);
    m_gridLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_gridLabel->setStyleSheet("background-color: #111;");
    m_gridLabel->installEventFilter(this);

    m_previewLabel = new QLabel(gridPage);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    m_previewLabel->setStyleSheet("background-color: #1a1a2e; border-left: 2px solid #444;");
    m_previewLabel->setMinimumWidth(180);
    m_previewLabel->setMaximumWidth(360);
    m_previewLabel->installEventFilter(this);

    gridLayout->addWidget(m_gridLabel, 3);
    gridLayout->addWidget(m_previewLabel, 1);
    m_stack->addWidget(gridPage);  // index 1

    mainLayout->addWidget(m_stack, 1);

    // Bottom controls
    QHBoxLayout *ctrlLayout = new QHBoxLayout();
    ctrlLayout->setContentsMargins(4, 0, 4, 4);
    m_labelLower  = new QLabel("–",   central);
    m_rangeSlider = new RangeSlider(central, Qt::Horizontal);
    m_labelUpper  = new QLabel("–",   central);
    m_labelSort   = new QLabel("–",   central);
    m_sortSlider  = new QSlider(Qt::Horizontal, central);
    m_sortSlider->setMaximumWidth(80);
    m_sortSlider->setMinimum(1);
    m_sortSlider->setValue(1);
    m_labelSpeed  = new QLabel("– ms", central);
    m_speedSlider = new QSlider(Qt::Horizontal, central);
    m_revers = new QCheckBox(central);
    m_revers->setText("rev");
    m_revers->setCheckable(true);
    m_revers->setCheckState(Qt::Unchecked);
    m_speedSlider->setMaximumWidth(80);
    m_speedSlider->setRange(10, 200);
    m_speedSlider->setValue(40);
    m_speedSlider->setEnabled(false);
    m_rangeSlider->blockSignals(true);
    m_sortSlider->blockSignals(true);
    m_speedSlider->blockSignals(true);
    ctrlLayout->addWidget(m_labelLower);
    ctrlLayout->addWidget(m_rangeSlider, 1);
    ctrlLayout->addWidget(m_labelUpper);
    ctrlLayout->addWidget(m_labelSort);
    ctrlLayout->addWidget(m_sortSlider);
    ctrlLayout->addWidget(m_labelSpeed);
    ctrlLayout->addWidget(m_speedSlider);
    ctrlLayout->addWidget(m_revers);
    mainLayout->addLayout(ctrlLayout);

    setCentralWidget(central);

    // Connections
    connect(m_label, &Label::rightClicked, this, &VideoWidget::toggleView);
    connect(m_label, &Label::cropChanged,  this, [this]
            {
                if (m_paused || m_playTimer.isActive() == false)
                    showFrame(m_playIndex);
            });
    m_playTimer.setTimerType(Qt::PreciseTimer);
    m_previewTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_previewTimer, &QTimer::timeout, this, &VideoWidget::previewTick);
    connect(&m_playTimer,    &QTimer::timeout, this, &VideoWidget::playTick);
    connect(m_rangeSlider, &RangeSlider::lowerValueChanged, this, &VideoWidget::lowerValueChanged);
    connect(m_rangeSlider, &RangeSlider::upperValueChanged, this, &VideoWidget::upperValueChanged);
    connect(m_sortSlider,  &QSlider::valueChanged,          this, &VideoWidget::sortValueChanged);
    connect(m_speedSlider, &QSlider::valueChanged,          this, &VideoWidget::speedValueChanged);
    setAcceptDrops(true);

    QMetaObject::invokeMethod(this, [this]
                              {
                                  // Beim Start den obersten "Zuletzt geöffnet"-Eintrag automatisch öffnen
                                  const QStringList opened = QSettings().value("history/files").toStringList();
                                  if (!opened.isEmpty()) doDropEvent(opened.first());
                              }, Qt::QueuedConnection);
}

// ─── View toggle ────────────────────────────────────────────────────────────

void VideoWidget::toggleView()
{
    if (m_bigMap.isEmpty()) return;
    m_showingGrid = !m_showingGrid;
    m_stack->setCurrentIndex(m_showingGrid ? 1 : 0);
    if (m_showingGrid)
    {
        m_playTimer.stop();
        paintGrid();
    }
    else
    {
        m_previewTimer.stop();
        if (!m_fillingMap) startPlayback();
        // updateTitle() wird von startPlayback() aufgerufen
    }
}

bool VideoWidget::eventFilter(QObject *obj, QEvent *event)
{
    if ((obj == m_gridLabel || obj == m_previewLabel)
        && event->type() == QEvent::MouseButtonRelease)
    {
        if (static_cast<QMouseEvent*>(event)->button() == Qt::RightButton)
        {
            toggleView();
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

// ─── Grid layout optimisation ────────────────────────────────────────────────

VideoWidget::GridDims VideoWidget::findOptimalGrid(int N) const
{
    if (N <= 0) return {1, 1};

    int    bestCols = 1, bestRows = N;
    double bestScore = 1e9;

    for (int cols = 1; cols <= N; ++cols)
    {
        const int baseRows = (N + cols - 1) / cols;
        for (int extra = 0; extra <= 1; ++extra) {          // try +0 and +1 extra row
            const int rows  = baseRows + extra;
            const int waste = cols * rows - N;

            // cell aspect ratio for a square texture
            const double cellAspect = static_cast<double>(rows) / cols;

            // symmetric log-ratio: 0 = perfect, grows for over- and under-stretch
            const double distortion = std::abs(std::log(cellAspect));

            // waste penalty is mild — user accepts +1 row/col
            const double score = distortion + waste * 0.15;

            if (score < bestScore)
            {
                bestScore = score;
                bestCols  = cols;
                bestRows  = rows;
            }
        }
    }
    return {bestCols, bestRows};
}

// ─── Playback through m_bigMap selection ─────────────────────────────────────

void VideoWidget::togglePause()
{
    if (m_fillingMap || m_bigMap.isEmpty()) return;
    m_paused = !m_paused;
    if (m_paused)
    {
        m_playTimer.stop();
    }
    else
    {
        // Fortsetzung an aktueller Position; nur außerhalb des Bereichs vom unteren Griff
        if (m_playIndex < m_rangeSlider->lowerValue() || m_playIndex >= m_rangeSlider->upperValue())
            m_playIndex = m_rangeSlider->lowerValue();
        m_playTimer.start(qMax(1, m_delay * m_sortSlider->value()));
    }
    updateTitle();
}

void VideoWidget::reduceMinMax()
{
    if (m_bigMap.isEmpty()) return;

    const int lower = m_rangeSlider->lowerValue();
    const int upper = m_rangeSlider->upperValue();
    if (lower <= 0 && upper >= m_bigMap.size() - 1) return; // nichts zu reduzieren

    const int newCount = upper - lower + 1;
    m_bigMap       = m_bigMap.mid(lower, newCount);
    m_bigMapBackup = m_bigMapBackup.mid(lower, newCount); // gelöschte Frames sind unwiederruflich weg

    m_playTimer.stop();
    m_previewTimer.stop();

    // Aktuelle Position relativ zum neuen Bereich erhalten
    m_playIndex = qBound(0, m_playIndex - lower, newCount - 1);

    m_rangeSlider->blockSignals(true);
    m_sortSlider->blockSignals(true);

    m_rangeSlider->setRange(0, newCount - 1);
    m_rangeSlider->setLowerValue(0);
    m_rangeSlider->setUpperValue(newCount - 1);
    m_rangeSlider->setValue(m_playIndex);

    const int maxSort = qMax(1, newCount / 2);
    const int newSort  = qBound(1, m_sortSlider->value(), maxSort);
    m_sortSlider->setRange(1, maxSort);
    m_sortSlider->setValue(newSort);

    m_rangeSlider->blockSignals(false);
    m_sortSlider->blockSignals(false);

    m_labelLower->setText("0");
    m_labelUpper->setText(QString::number(newCount - 1));
    m_labelSort->setText(QString("%1/%2").arg(newSort).arg(maxSort));

    if (m_showingGrid)
    {
        paintGrid();
    }
    else
    {
        showFrame(m_playIndex);
        if (!m_paused)
            m_playTimer.start(qMax(1, m_delay * newSort));
    }
    updateTitle();
}


void VideoWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space)
        togglePause();
    else
        QMainWindow::keyPressEvent(event);
}

void VideoWidget::startPlayback()
{
    m_playIndex = m_rangeSlider->lowerValue();
    if (!m_paused)
        m_playTimer.start(qMax(1, m_delay * m_sortSlider->value()));
    updateTitle();
}

void VideoWidget::playTick()
{
    auto sliders = getSliderValues();

    if (m_playIndex < 0 || m_playIndex >= m_bigMap.size())
    {
        m_playIndex = sliders.first;
    }

    const QRect cropRect = m_label->cropRectInImageCoords();

    QPixmap px = m_bigMap[m_playIndex];
    if(cropRect.isValid())
    {
        px = px.copy(cropRect);
    }
    m_label->setImage(px, m_playIndex, m_bigMap.size(), m_delay);
    m_label->update();

    m_rangeSlider->setValue(m_playIndex);   // blauen Balken mit Wiedergabe mitlaufen lassen

    if(m_revers->isChecked())
    {
        if(m_isReversed)
        {
            m_playIndex -= sliders.step;
            if (m_playIndex < sliders.first)
            {
                m_playIndex = sliders.first;
                m_isReversed = false;
            }
        }
        else
        {
            m_playIndex += sliders.step;
            if (m_playIndex > sliders.last)
            {
                m_playIndex = sliders.last;
                m_isReversed = true;
            }
        }
    }
    else
    {
        m_playIndex += sliders.step;
        if (m_playIndex > sliders.last)
        {
            m_playIndex = sliders.first;
        }
    }
}

// ─── Grid composing ─────────────────────────────────────────────────────────

QPixmap VideoWidget::composeGrid(int first, int frameCount, int step)
{
    const int N = qMax(1, frameCount / step);
    m_grid = findOptimalGrid(N);

    const int cellW = m_resolution / m_grid.cols;
    const int cellH = m_resolution / m_grid.rows;

    QPixmap result(m_grid.cols*cellW, m_grid.rows*cellH); // so groß wie nötig, damit alle Frames reinpassen
    result.fill(Qt::transparent);
    QPainter p(&result);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    m_previewList.clear();
    const QRect cropRect = m_label->cropRectInImageCoords();

    for (int i = 0; i < N; ++i)
    {
        const int key = first + i * step;
        if (key < 0 || key >= m_bigMap.size()) continue;

        // Apply crop if one is set
        QPixmap src = m_bigMap[key];
        if (!cropRect.isEmpty() && cropRect != src.rect())
            src = src.copy(cropRect);

        m_previewList << src;

        const int row = i / m_grid.cols;
        const int col = i % m_grid.cols;
        // const QRect cell(round(col * cellW), round(row * cellH), round(cellW), round(cellH));
        const QRect cell(col * cellW, row * cellH, cellW, cellH);
        p.drawPixmap(cell, src.scaled(cell.size(),
                                      Qt::IgnoreAspectRatio,
                                      Qt::SmoothTransformation));
    }
    const double cellAspect = static_cast<double>(m_grid.rows) / m_grid.cols;
    setWindowTitle(
        QString("Frame Grabber  —  %1×%2  |  %3 frames  |  Stretch %4×  |  Verschnitt %5")
            .arg(m_grid.cols).arg(m_grid.rows).arg(N)
            .arg(QString::number(cellAspect, 'f', 2)));

    return result;
}

void VideoWidget::paintGrid()
{
    if (m_bigMap.isEmpty()) return;
    auto sliders = getSliderValues();

    const QPixmap grid = composeGrid(sliders.first, sliders.last - sliders.first + 1, sliders.step);
    m_gridLabel->setPixmap(grid.scaled(
        m_gridLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

    // Start preview animation
    m_previewIndex = 0;
    if (!m_previewList.isEmpty())
    {
        m_previewLabel->setPixmap(m_previewList[0].scaled(
            m_previewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        const int interval = qMax(1, m_delay * sliders.step);
        m_previewTimer.start(interval);
    }
}

// ─── Slider slots ───────────────────────────────────────────────────────────

void VideoWidget::showFrame(int index)
{
    if (index < 0 || index >= m_bigMap.size()) return;
    QPixmap px = m_bigMap[index];
    const QRect cr = m_label->cropRectInImageCoords();
    if (!cr.isEmpty() && cr != px.rect()) px = px.copy(cr);
    m_label->setImage(px, index, m_bigMap.size(), m_delay);
    m_label->update();
}

void VideoWidget::lowerValueChanged(int value)
{
    m_labelLower->setText(QString::number(value));
    if (value >= m_rangeSlider->upperValue())
    {
        m_rangeSlider->setLowerValue(m_rangeSlider->upperValue() - 1);
        return;
    }
    m_lastHandle = LowerHandle;
    if (m_showingGrid)
    {
        paintGrid();
    }
    else if (!m_fillingMap)
    {
        // Slider bewegt → automatisch pausieren und Frame zeigen
        m_paused = true;
        m_playTimer.stop();
        m_playIndex = value;
        m_rangeSlider->setValue(value);   // blauen Balken mitziehen
        showFrame(value);
    }
    updateTitle();
}

void VideoWidget::upperValueChanged(int value)
{
    m_labelUpper->setText(QString::number(value));
    if (value <= m_rangeSlider->lowerValue())
    {
        m_rangeSlider->setUpperValue(m_rangeSlider->lowerValue() + 1);
        return;
    }
    m_lastHandle = UpperHandle;
    if (m_showingGrid)
    {
        paintGrid();
    }
    else if (!m_fillingMap)
    {
        // Slider bewegt → automatisch pausieren und Frame zeigen
        m_paused = true;
        m_playTimer.stop();
        m_playIndex = value;
        m_rangeSlider->setValue(value);   // blauen Balken mitziehen
        showFrame(value);
    }
    updateTitle();
}

void VideoWidget::sortValueChanged(int value)
{
    const int maxSort = qMax(1, (m_rangeSlider->upperValue()
                                 - m_rangeSlider->lowerValue() + 1) / 2);
    m_labelSort->setText(QString("%1/%2").arg(value).arg(maxSort));
    if (m_showingGrid)
        paintGrid();
    else if (!m_fillingMap && !m_paused)
        m_playTimer.start(qMax(1, m_delay * value));
    updateTitle();
}

void VideoWidget::speedValueChanged(int value)
{
    m_delay = value;
    m_labelSpeed->setText(QString("%1 ms").arg(value));
    if (m_showingGrid)
        paintGrid();
    else if (!m_fillingMap && !m_paused)
        m_playTimer.start(qMax(1, m_delay * m_sortSlider->value()));
    updateTitle();
}

// ─── Preview animation ───────────────────────────────────────────────────────

void VideoWidget::previewTick()
{
    if (m_previewList.isEmpty()) return;
    m_previewIndex = (m_previewIndex + 1) % m_previewList.size();

    const int cellW = m_resolution / m_grid.cols;
    const int cellH = m_resolution / m_grid.rows;

    // Scale to actual sprite-sheet cell size (= real quality in SL texture)
    const QPixmap cellSized = m_previewList[m_previewIndex].scaled(
        cellW, cellH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    // Upscale without smoothing → pixelation visible = honest quality preview
    m_previewLabel->setPixmap(cellSized.scaled(
        m_previewLabel->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
}

// ─── Drag & Drop ────────────────────────────────────────────────────────────

void VideoWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void VideoWidget::dropEvent(QDropEvent *event)
{
    auto &m=*event->mimeData();
    QString lastWebsite;
    QUrl url;
    if(m.hasUrls())
    {
        url = m.urls().first();
        if(url.isLocalFile())
        {
            doDropEvent(url.toLocalFile());
            return;
        }
        lastWebsite = url.toString(QUrl::RemoveQuery | QUrl::RemoveFragment);
        QRegularExpression re(R"(\.(mp4|gif|webm|png|jpg|jpeg|bmp|tif|tiff|webp)$)",
                              QRegularExpression::CaseInsensitiveOption);
        if(re.match(lastWebsite).hasMatch())
        {
            doDropEvent(url.toString());
            return;
        }
    }
    if(m.hasHtml())
    {
        const QString html = m.html();
        QRegularExpression re(R"(https://[^\s"'<>]+\.(?:mp4|gif|webm))",
                              QRegularExpression::CaseInsensitiveOption);
        auto match = re.match(html);
        if(not match.hasMatch())
        {   // zweiter versuch mit Bildern, falls kein Video gefunden wurde
            QRegularExpression re(R"(https://[^\s"'<>]+\.(?:png|jpg|jpeg|bmp|tif|tiff|webp))",
                                  QRegularExpression::CaseInsensitiveOption);
            match = re.match(html);
        }
        if(match.hasMatch())
        {
            if(lastWebsite.isEmpty())
            {
                doDropEvent(match.captured(0));
            } else
            {
                doDropEvent(match.captured(0)+","+lastWebsite);
            }
            return;
        } else
        {
            // kein Link in HTML gefunden, versuchen, den html direkt von der url zu lesen
        }
    }
    // website von lastWebsite lesen
    QNetworkAccessManager nam;
    QNetworkRequest request(url);
    QNetworkReply *reply = nam.get(request);
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (reply->error() == QNetworkReply::NoError)
    {
        const QByteArray data = reply->readAll();
        const QString html=QString::fromUtf8(data);
        QRegularExpression re(R"(https://[^\s"'<>]+\.(?:mp4|gif|webm))",
                              QRegularExpression::CaseInsensitiveOption);
        auto match = re.match(html);
        if(not match.hasMatch())
        {   // zweiter versuch mit Bildern, falls kein Video gefunden wurde
            QRegularExpression re(R"(https://[^\s"'<>]+\.(?:png|jpg|jpeg|bmp|tif|tiff|webp))",
                                  QRegularExpression::CaseInsensitiveOption);
            match = re.match(html);
        }
        if(match.hasMatch())
        {
            if(lastWebsite.isEmpty())
            {
                doDropEvent(match.captured(0));
            } else
            {
                doDropEvent(match.captured(0)+","+lastWebsite);
            }
        }
    }
}

void VideoWidget::doDropEvent(const QString &pathAndUrl)
{
    if (pathAndUrl.isEmpty()) return;
    setLoadingFile(pathAndUrl);
    const QString path=pathAndUrl.split(",").first();
    resetExportNameForVideo(path); // neues Video → Dateiname-Vorgabe neu setzen
    m_label->setDefaults();
    m_previewTimer.stop();
    m_playTimer.stop();
    if (m_extractor) { m_extractor->cancel(); m_extractor->deleteLater(); m_extractor = nullptr; }
    if (m_bgRemover) { m_bgRemover->cancel(); m_bgRemover->deleteLater(); m_bgRemover = nullptr; }
    m_actBgRemove->setText("Hintergrund entfernen (ComfyUI)");
    m_actBgRemove->setEnabled(false);

    m_bigMap.clear();
    m_previewList.clear();
    m_delay       = 0;
    m_fillingMap  = true;
    m_paused      = false;
    m_lastHandle  = NoHandle;
    m_showingGrid = false;
    m_stack->setCurrentIndex(0);
    m_rangeSlider->setEnabled(false);
    m_sortSlider->setEnabled(false);
    m_speedSlider->setEnabled(false);
    m_rangeSlider->blockSignals(true);
    m_sortSlider->blockSignals(true);
    m_speedSlider->blockSignals(true);
    m_labelLower->setText("–");
    m_labelUpper->setText("–");
    m_labelSort->setText("–");
    m_labelSpeed->setText("– ms");
    setWindowTitle("VideoConverter — extrahiere Frames …");

    static const QStringList imageExts = {"png","jpg","jpeg","bmp","tif","tiff","webp"};
    const QString ext = QFileInfo(path).suffix().toLower();

    if (imageExts.contains(ext))
    {
        // Einzelbild → sofort laden
        addToHistory(pathAndUrl);
        setLoadingFile("");
        const QPixmap px(path);
        if (px.isNull()) return;
        onFramesExtracted({px}, 40);
    }
    else
    {
        // Video oder GIF → FrameExtractor
        m_extractor = new FrameExtractor("ffmpeg", this);
        connect(m_extractor, &FrameExtractor::progress, this, [this](int done, int total)
                {
                    setWindowTitle(QString("VideoConverter — extrahiere Frames … (%1/%2)")
                                       .arg(done).arg(total));
                });
        connect(m_extractor, &FrameExtractor::finished,
                this, &VideoWidget::onFramesExtracted);
        connect(m_extractor, &FrameExtractor::error, this, [this](const QString &msg)
                {
                    setWindowTitle("VideoConverter");
                    m_fillingMap = false;
                    QMessageBox::warning(this, "Extraktion fehlgeschlagen", msg);
                });
        m_extractor->extract(path);
    }
}

void VideoWidget::onFramesExtracted(QVector<QPixmap> frames, int delayMs)
{
    if (m_extractor)
    {
        m_extractor->deleteLater();
        m_extractor = nullptr;
    }

    if (frames.isEmpty())
    {
        setWindowTitle("VideoConverter");
        m_fillingMap = false;
        return;
    }

    addToHistory(loadingFile(), frames.first()); // Originalframe — addToHistory cappt auf kPreviewMaxSize
    setLoadingFile("");
    m_bigMap  = frames;
    m_bigMapBackup = frames;
    m_delay   = delayMs;
    const int count = m_bigMap.size();

    m_rangeSlider->setRange(0, count - 1);
    m_rangeSlider->setLowerValue(0);
    m_rangeSlider->setUpperValue(count - 1);
    const int maxSort = qMax(1, count / 2);
    m_sortSlider->setRange(1, maxSort);
    m_sortSlider->setValue(1);
    const int clampedDelay = qBound(m_speedSlider->minimum(), delayMs, m_speedSlider->maximum());
    m_speedSlider->setValue(clampedDelay);
    m_delay = clampedDelay;
    m_labelLower->setText("0");
    m_labelUpper->setText(QString::number(count - 1));
    m_labelSort->setText(QString("1/%1").arg(maxSort));
    m_labelSpeed->setText(QString("%1 ms").arg(m_delay));
    m_rangeSlider->blockSignals(false);
    m_sortSlider->blockSignals(false);
    m_speedSlider->blockSignals(false);
    m_rangeSlider->setEnabled(true);
    m_sortSlider->setEnabled(true);
    m_speedSlider->setEnabled(true);
    m_fillingMap = false;
    m_actBgRemove->setEnabled(true);

    // Erstes Bild anzeigen
    showFrame(0);
    startPlayback();
}

// ─── Undo ────────────────────────────────────────────────────────────────────

void VideoWidget::updateTitle()
{
    if (m_fillingMap || m_bigMap.isEmpty()) return;
    auto sliders = getSliderValues();
    const double fps = sliders.step > 0 && m_delay > 0 ? 1000.0 / (m_delay * sliders.step) : 0.0;
    const QString status = m_paused ? "  ⏸ PAUSE" : "";
    setWindowTitle(QString("VideoConverter  —  [%1 … %2]  step %3  |  %6*%7  |  %4 fps%5")
                       .arg(sliders.first).arg(sliders.last).arg(sliders.step)
                       .arg(fps, 0, 'f', 1).arg(status)
                       .arg(m_bigMap[0].width()).arg(m_bigMap[0].height()));
}

// ─── Background removal ───────────────────────────────────────────────────────

void VideoWidget::startBgRemoval()
{
    if (m_bigMap.isEmpty()) return;

    // Only process frames in current slider range with step
    auto sliders = getSliderValues();

    QMap<int, QPixmap> toProcess;
    for (int i = sliders.first; i <= sliders.last; i += sliders.step)
    {
        if (i >= 0 && i < m_bigMap.size())
        {
            toProcess[i] = m_bigMap[i];
        }
    }
    if (toProcess.isEmpty()) return;

    m_actBgRemove->setText("Abbrechen");
    setWindowTitle(QString("Hintergrund wird entfernt … (0/%1)").arg(toProcess.size()));

    m_bgRemover = new ComfyBgRemover("http://127.0.0.1:8188", this);
    connect(m_bgRemover, &ComfyBgRemover::frameReady, this, &VideoWidget::onBgFrameReady);
    connect(m_bgRemover, &ComfyBgRemover::progress,   this, &VideoWidget::onBgProgress);
    connect(m_bgRemover, &ComfyBgRemover::finished,   this, &VideoWidget::onBgFinished);
    connect(m_bgRemover, &ComfyBgRemover::error, this, [this](const QString &msg)
            {
                setWindowTitle("Fehler: " + msg);
                m_actBgRemove->setText("Hintergrund entfernen (ComfyUI)");
                m_bgRemover->deleteLater();
                m_bgRemover = nullptr;
            });

    const QAction *checkedModel = m_modelGroup->checkedAction();
    const QString model = checkedModel ? checkedModel->text() : "ZhengPeng7/BiRefNet";
    const QAction *checkedNode = m_nodeGroup->checkedAction();
    const QString nodeType = checkedNode ? checkedNode->text() : "BiRefNet_Hugo";
    m_bgRemover->process(toProcess, model, nodeType);
}

void VideoWidget::onBgFrameReady(int index, QPixmap result)
{
    if (index < 0 || index >= m_bigMap.size()) return;
    m_bigMap[index] = result;
    // Show current frame live if it's visible
    if (!m_showingGrid && m_label->currentIndex() == index)
    {
        m_label->setImage(result, index, m_bigMap.size(), m_delay);
        m_label->update();
    }
}

void VideoWidget::onBgProgress(int done, int total)
{
    setWindowTitle(QString("Hintergrund wird entfernt … (%1/%2)").arg(done).arg(total));
}

void VideoWidget::onBgFinished()
{
    setWindowTitle("VideoConverter");
    m_actBgRemove->setText("Hintergrund entfernen (ComfyUI)");
    m_bgRemover->deleteLater();
    m_bgRemover = nullptr;
    if (m_showingGrid)
        paintGrid();
    else
    {
        const int cur = m_rangeSlider->lowerValue();
        if (cur >= 0 && cur < m_bigMap.size())
        {
            m_label->setImage(m_bigMap[cur], cur, m_bigMap.size(), m_delay);
            m_label->update();
        }
    }
}

namespace
{
// Oberster Ordner im Pfad, der noch nicht existiert – leer, falls der ganze
// Pfad schon vorhanden ist. Damit lässt sich merken, was mkpath neu anlegt.
QString highestMissingDir(const QString &fullPath)
{
    QString p = QDir::cleanPath(fullPath);
    QString highest;
    while (!p.isEmpty() && !QDir(p).exists())
    {
        highest = p;
        const QString parent = QFileInfo(p).path();
        if (parent == p) break;
        p = parent;
    }
    return highest;
}

// Entfernt die zuvor temporär angelegten (leeren) Ordner von unten nach oben,
// bis einschließlich 'highest'. Nicht-leere Ordner bleiben unangetastet.
void removeCreatedDirs(const QString &fullPath, const QString &highest)
{
    if (highest.isEmpty()) return;
    QString p = QDir::cleanPath(fullPath);
    while (!p.isEmpty())
    {
        if (QDir(p).exists())
        {
            if (!QDir(p).isEmpty()) break;   // es liegt etwas drin → stehen lassen
            QDir().rmdir(p);
        }
        if (QDir::cleanPath(p) == QDir::cleanPath(highest)) break;
        const QString parent = QFileInfo(p).path();
        if (parent == p) break;
        p = parent;
    }
}
} // namespace

void VideoWidget::exportDialog()
{
    if (m_bigMap.isEmpty()) return;

    QSettings s;
    const QString curPath = lastFile().split(",").first();
    const QString defBase = QFileInfo(curPath).completeBaseName();       // Name des aktuellen Videos
    const QString defDir  = s.value("export/dir",
                                    QFileInfo(curPath).absolutePath()).toString(); // zuletzt genutzter Pfad

    QDialog dlg(this);
    dlg.setWindowTitle("Exportieren");

    // 1. Dateiname (gemerkter Wert, sonst Name des aktuellen Videos)
    QLineEdit *nameEdit = new QLineEdit(s.value("export/name", defBase).toString(), &dlg);

    // 1b. Subordner (optional): verschiebt den Dateinamen in einen Unterordner
    QCheckBox *cbSub   = new QCheckBox("Subordner", &dlg);
    QLineEdit *subEdit = new QLineEdit(&dlg);
    const bool subOn = s.value("export/subOn", false).toBool();
    subEdit->setText(s.value("export/sub").toString());
    cbSub->setChecked(subOn);
    subEdit->setEnabled(subOn);
    // Toggle erst nach dem Setzen des Startzustands verbinden
    connect(cbSub, &QCheckBox::toggled, &dlg, [nameEdit, subEdit](bool on)
    {
        if (on)
        {
            subEdit->setText(nameEdit->text());   // Dateiname → Subordner
            subEdit->setEnabled(true);
            nameEdit->setText("video");
        }
        else
        {
            nameEdit->setText(subEdit->text());   // Subordner → Dateiname zurück
            subEdit->clear();
            subEdit->setEnabled(false);
        }
    });

    // 2. Pfad (voreingestellt mit zuletzt genutztem Ausgabepfad)
    QLineEdit   *dirEdit   = new QLineEdit(defDir, &dlg);
    QPushButton *browseBtn = new QPushButton("…", &dlg);
    browseBtn->setFixedWidth(32);
    connect(browseBtn, &QPushButton::clicked, &dlg, [&dlg, dirEdit, subEdit, cbSub]()
    {
        // Bei aktivem Subordner startet der Dialog in Ordner/Subordner; dazu
        // wird dieser Pfad temporär angelegt und bei Abbruch wieder entfernt.
        QString start = dirEdit->text();
        QString created;
        const QString sub = subEdit->text().trimmed();
        if (cbSub->isChecked() && !sub.isEmpty())
        {
            start   = QDir::cleanPath(dirEdit->text() + "/" + sub);
            created = highestMissingDir(start);
            QDir().mkpath(start);
        }
        const QString d = QFileDialog::getExistingDirectory(
            &dlg, "Zielordner wählen", start);
        if (!d.isEmpty())
            dirEdit->setText(d);
        else
            removeCreatedDirs(start, created); // Abbruch → temporäre Ordner entfernen
    });
    QHBoxLayout *dirRow = new QHBoxLayout;
    dirRow->addWidget(dirEdit);
    dirRow->addWidget(browseBtn);

    // 3. Mehrfachauswahl der Formate
    QCheckBox *cbWebm = new QCheckBox("WEBM", &dlg);
    QCheckBox *cbMp4  = new QCheckBox("MP4",  &dlg);
    QCheckBox *cbGif  = new QCheckBox("GIF",  &dlg);
    QCheckBox *cbPng  = new QCheckBox("PNG",  &dlg);
    cbWebm->setChecked(s.value("export/webm", true).toBool());
    cbMp4 ->setChecked(s.value("export/mp4",  true).toBool());
    cbGif ->setChecked(s.value("export/gif",  true).toBool());
    cbPng ->setChecked(s.value("export/png",  true).toBool());
    cbPng->setToolTip("Sprite-Sheet mit Zusatzinfos für die TextureAnimation");

    QHBoxLayout *fmtRow = new QHBoxLayout;
    fmtRow->addWidget(cbWebm);
    fmtRow->addWidget(cbMp4);
    fmtRow->addWidget(cbGif);
    fmtRow->addWidget(cbPng);
    fmtRow->addStretch();

    QFormLayout *form = new QFormLayout;
    form->addRow("Dateiname:", nameEdit);
    form->addRow(cbSub,        subEdit);
    form->addRow("Ordner:",    dirRow);
    form->addRow("Formate:",   fmtRow);

    // 4. Export starten oder abbrechen
    QDialogButtonBox *bb = new QDialogButtonBox(&dlg);
    bb->addButton("Export",    QDialogButtonBox::AcceptRole);
    bb->addButton("Abbrechen", QDialogButtonBox::RejectRole);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    QVBoxLayout *lay = new QVBoxLayout(&dlg);
    lay->addLayout(form);
    lay->addWidget(bb);

    // Bei jeder Änderung prüfen, ob eine der Zieldateien schon existiert.
    // Dateiname-Feld wird dann rot, Tooltip nennt die betroffenen Dateien.
    auto validate = [this, nameEdit, subEdit, cbSub, dirEdit,
                     cbWebm, cbMp4, cbGif, cbPng]()
    {
        QString dir = dirEdit->text().trimmed();
        const QString sub = subEdit->text().trimmed();
        if (cbSub->isChecked() && !sub.isEmpty())
            dir = QDir::cleanPath(dir + "/" + sub);

        const QString base = nameEdit->text().trimmed();
        const QStringList existing = base.isEmpty() ? QStringList()
            : existingExportTargets(dir, base, true, true, true, true); // immer alle Formate prüfen
        if (existing.isEmpty())
        {
            nameEdit->setStyleSheet(QString());
            nameEdit->setToolTip(QString());
        }
        else
        {
            nameEdit->setStyleSheet("background-color:#ffb0b0;");
            nameEdit->setToolTip("Diese Dateien existieren bereits und werden "
                                 "beim Export überschrieben:\n" + existing.join("\n"));
        }
    };
    connect(nameEdit, &QLineEdit::textChanged, &dlg, validate);
    connect(subEdit,  &QLineEdit::textChanged, &dlg, validate);
    connect(dirEdit,  &QLineEdit::textChanged, &dlg, validate);
    connect(cbSub,  &QCheckBox::toggled, &dlg, validate);
    connect(cbWebm, &QCheckBox::toggled, &dlg, validate);
    connect(cbMp4,  &QCheckBox::toggled, &dlg, validate);
    connect(cbGif,  &QCheckBox::toggled, &dlg, validate);
    connect(cbPng,  &QCheckBox::toggled, &dlg, validate);
    validate();

    const int result = dlg.exec();

    // Parameter immer merken – bleiben so auch nach Abbruch erhalten
    s.setValue("export/name",  nameEdit->text());
    s.setValue("export/subOn", cbSub->isChecked());
    s.setValue("export/sub",   subEdit->text());
    s.setValue("export/dir",   dirEdit->text());
    s.setValue("export/webm",  cbWebm->isChecked());
    s.setValue("export/mp4",   cbMp4->isChecked());
    s.setValue("export/gif",   cbGif->isChecked());
    s.setValue("export/png",   cbPng->isChecked());

    if (result != QDialog::Accepted) return;

    const QString base = nameEdit->text().trimmed();
    QString dir        = dirEdit->text().trimmed();
    if (base.isEmpty() || dir.isEmpty()) return;
    const QString sub = subEdit->text().trimmed();
    if (cbSub->isChecked() && !sub.isEmpty())
        dir = QDir::cleanPath(dir + "/" + sub);   // Export in Ordner/Subordner
    if (!cbWebm->isChecked() && !cbMp4->isChecked()
        && !cbGif->isChecked() && !cbPng->isChecked())
    {
        QMessageBox::information(this, "Export", "Es wurde kein Format ausgewählt.");
        return;
    }

    runExport(dir, base,
              cbWebm->isChecked(), cbMp4->isChecked(),
              cbGif->isChecked(),  cbPng->isChecked());
}

void VideoWidget::runExport(const QString &dir, const QString &baseName,
                            bool webm, bool mp4, bool gif, bool png)
{
    QDir().mkpath(dir);
    const QString stem = dir + "/" + baseName;
    if (webm) saveVideo(stem + ".webm");
    if (mp4)  saveVideo(stem + ".mp4");
    if (gif)  saveVideo(stem + ".gif");
    if (png)  saveSpriteSheet(stem + ".png");
}

QString VideoWidget::spriteFileName(const QString &pngPath) const
{
    auto sliders = getSliderValues();
    const int frameCount = sliders.last - sliders.first + 1;
    const int N = qMax(1, frameCount / sliders.step);
    const GridDims g = findOptimalGrid(N);
    int frames = 0;
    for (int i = 0; i < N; ++i)
    {
        const int key = sliders.first + i * sliders.step;
        if (key >= 0 && key < m_bigMap.size()) ++frames;
    }
    const double fps = m_delay > 0 ? 1000.0 / (m_delay * sliders.step) : 25.0;
    QString out(pngPath);
    out.replace(".png", QString("(%1_%2_%3_%4).png")
                    .arg(g.cols).arg(g.rows).arg(frames).arg(int(fps)));
    return out;
}

QStringList VideoWidget::existingExportTargets(const QString &dir, const QString &base,
                                               bool webm, bool mp4, bool gif, bool png) const
{
    QStringList targets;
    const QString stem = dir + "/" + base;
    if (webm) targets << stem + ".webm";
    if (mp4)  targets << stem + ".mp4";
    if (gif)  targets << stem + ".gif";
    if (png)  targets << spriteFileName(stem + ".png");

    QStringList existing;
    for (const QString &t : targets)
        if (QFileInfo::exists(t)) existing << t;
    return existing;
}

void VideoWidget::saveVideo(const QString &path)
{
    auto sliders = getSliderValues();

    VideoExporter::Options opts;
    opts.fps        = (sliders.step > 0 && m_delay > 0) ? 1000.0 / (m_delay * sliders.step) : 25.0;
    opts.outputPath = path;
    if (path.endsWith(".mp4", Qt::CaseInsensitive))      opts.format = VideoExporter::MP4;
    else if (path.endsWith(".gif", Qt::CaseInsensitive)) opts.format = VideoExporter::GIF;
    else if (path.endsWith(".webm", Qt::CaseInsensitive)) opts.format = VideoExporter::WebM;
    else
    {
        QMessageBox::warning(this, "Export-Fehler", "Unbekanntes Format:\n" + path);
        return;
    }

    // Collect frames in slider range with step + crop applied

    const QRect cropRect = m_label->cropRectInImageCoords();

    QMap<int, QPixmap> toExport;
    int idx = 0;
    for (int i = sliders.first; i <= sliders.last; i += sliders.step)
    {
        if (i < 0 || i >= m_bigMap.size()) continue;
        QPixmap px = m_bigMap[i];
        if (!cropRect.isEmpty() && cropRect != px.rect())
            px = px.copy(cropRect);
        toExport[idx++] = px;
    }
    if (toExport.isEmpty()) return;

    auto *exporter = new VideoExporter(this);
    connect(exporter, &VideoExporter::progress, this, [this](int done, int total)
            {
                setWindowTitle(QString("Exportiere … (%1/%2)").arg(done).arg(total));
            });
    connect(exporter, &VideoExporter::finished, this, [this, exporter](const QString &out)
            {
                setWindowTitle("VideoConverter");
                addToExportHistory(out); // nur exportierte Videos merken (kein PNG)
                exporter->deleteLater();
                QMessageBox::information(this, "Export fertig", "Gespeichert:\n" + out);
            });
    connect(exporter, &VideoExporter::error, this, [this, exporter](const QString &msg)
            {
                setWindowTitle("VideoConverter");
                exporter->deleteLater();
                QMessageBox::warning(this, "Export-Fehler", msg);
            });

    exporter->exportFrames(toExport, opts);
}

void VideoWidget::saveSpriteSheet(const QString &path)
{
    auto sliders = getSliderValues();
    const QPixmap grid = composeGrid(sliders.first, sliders.last - sliders.first + 1, sliders.step);

    const QString pathExt = spriteFileName(path);
    if (grid.save(pathExt))
    {
        // QSettings().setValue("save/dir", QFileInfo(path).absolutePath());
        // addToExported(path);
        // addToHistory(QFileInfo(path).absolutePath());
        QMessageBox::information(this, "Export fertig", "Gespeichert:\n" + path);
    }
    else
    {
        QMessageBox::warning(this, "Export-Fehler", "Konnte nicht speichern:\n" + path);
        return;
    }
}

// ─── File open / history ─────────────────────────────────────────────────────

void VideoWidget::openFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "Datei öffnen", lastFile(),
        "Videos (*.mp4 *.mov *.avi *.mkv *.gif *.webm);;"
        "Bilder (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp)");
    if (!path.isEmpty()) doDropEvent(path);
}

void VideoWidget::addToHistory(const QString &pathAndUrl, const QPixmap &preview)
{
    if (pathAndUrl.isEmpty()) return; // kein Leereintrag → history/files bleibt lesbares REG_MULTI_SZ
    setLastFile(pathAndUrl); // damit es in "Zuletzt geöffnet" auftaucht
    QSettings s;
    QStringList parts = pathAndUrl.split(","); // Pfad und evtl URL trennen
    QStringList list = s.value("history/files").toStringList();
    list.removeAll(QString()); // evtl. vorhandene Alt-Leereinträge einmalig bereinigen
    int index=list.indexOf(QRegularExpression(parts[0]+".*")); // vorhandenen Eintrag mit gleichem Pfad finden (unabhängig von URL)
    if(index>=0)
    {
        list.removeAt(index); // bereits vorhandenen Eintrag entfernen, damit er weiter vorne landet
    }
    list.prepend(pathAndUrl);
    if (list.size() > 20) list.resize(20);
    s.setValue("history/files", list);

    if (!preview.isNull())
    {
        // Aufrufer liefert bereits einen Frame (z. B. aus onFramesExtracted) → direkt in den Cache.
        // Nur runter-, nie hochskalieren.
        const QPixmap capped = (preview.width() > kPreviewMaxSize || preview.height() > kPreviewMaxSize)
                                   ? preview.scaled(QSize(kPreviewMaxSize, kPreviewMaxSize),
                                                    Qt::KeepAspectRatio, Qt::SmoothTransformation)
                                   : preview;
        m_previewCache.insert(parts[0], capped);
    }
    else if (!m_previewCache.contains(parts[0]))
    {
        enqueuePreviewExtraction(parts[0]);
    }
    rebuildRecentMenu();
}

void VideoWidget::enqueuePreviewExtraction(const QString &path)
{
    if (path.isEmpty() || m_previewCache.contains(path) || m_previewQueue.contains(path))
        return;
    const bool wasIdle = m_previewQueue.isEmpty();
    m_previewQueue.append(path);
    if (wasIdle)
        m_previewExtractor->extractFirstFrame(path, QSize(kPreviewMaxSize, kPreviewMaxSize));
}

void VideoWidget::onPreviewReady(QString sourcePath, QPixmap preview)
{
    m_previewQueue.removeAll(sourcePath);
    if (!preview.isNull())
    {
        m_previewCache.insert(sourcePath, preview);
        rebuildRecentMenu();
    }
    if (!m_previewQueue.isEmpty())
        m_previewExtractor->extractFirstFrame(m_previewQueue.first(),
                                              QSize(kPreviewMaxSize, kPreviewMaxSize));
}


void VideoWidget::rebuildRecentMenu()
{
    m_recentMenu->clear();
    const QStringList hist = QSettings().value("history/files").toStringList();
    if (hist.isEmpty())
    {
        QAction *empty = m_recentMenu->addAction("(leer)");
        empty->setEnabled(false);
        return;
    }
    for (const QString &f : hist)
    {
        const QString filePart = f.split(",").first(); // Pfad und evtl URL trennen
        const QIcon previewIcon(m_previewCache.value(filePart));
        m_recentMenu->addAction(previewIcon,
            QString("%1\t%2").arg(QUrl(filePart).toString(QUrl::RemoveFilename),QFileInfo(filePart).fileName()),
            QKeySequence(),
            [this, f]
            {
                doDropEvent(f);
            });
    }
}

void VideoWidget::resetExportNameForVideo(const QString &path)
{
    const QString base = QFileInfo(path).completeBaseName();
    if (base.isEmpty()) return;
    QSettings s;
    if (s.value("export/subOn", false).toBool())
    {
        s.setValue("export/sub",  base);   // im Subordner-Modus wandert der Name in den Subordner
        s.setValue("export/name", "video");
    }
    else
    {
        s.setValue("export/name", base);
    }
}

void VideoWidget::addToExportHistory(const QString &path)
{
    QSettings s;
    QStringList list = s.value("export/history").toStringList();
    list.removeAll(path); // vorhandenen Eintrag entfernen, damit er wieder nach vorne rutscht
    list.prepend(path);
    if (list.size() > 20) list.resize(20);
    s.setValue("export/history", list);
    rebuildExportMenu();
}

void VideoWidget::rebuildExportMenu()
{
    if (!m_exportMenu) return;
    m_exportMenu->clear();
    const QStringList hist = QSettings().value("export/history").toStringList();
    if (hist.isEmpty())
    {
        QAction *empty = m_exportMenu->addAction("(leer)");
        empty->setEnabled(false);
        return;
    }
    for (const QString &f : hist)
    {
        m_exportMenu->addAction(
            QString("%1\t%2").arg(QFileInfo(f).absolutePath(), QFileInfo(f).fileName()),
            this,
            [this, f]
            {
                doDropEvent(f);
            });
    }
}

// ─── Open with ───────────────────────────────────────────────────────────────

void VideoWidget::openWithExplorer()
{
    if (lastFile().isEmpty())
    {
        QMessageBox::information(this, "Kein Pfad", "Noch kein Speicherpfad bekannt.");
        return;
    }
    const QStringList sl=lastFile().split(",");
    QUrl url(sl.first());
    if(sl.size()>1)
    {
        QDesktopServices::openUrl(QUrl(sl.last()));
        return;
    }
    if(url.isLocalFile())
    {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(sl.first()).absolutePath()));
        return;
    }
    QMessageBox::information(this, "Keine website", "website unbekannt.");
}

void VideoWidget::openWithUrl()
{
    if (lastFile().isEmpty())
    {
        QMessageBox::information(this, "Kein Pfad", "Noch kein Speicherpfad bekannt.");
        return;
    }
    QDesktopServices::openUrl(lastFile());
}

void VideoWidget::openWithXnView()
{
    openWithViewer("viewer/xnview", "XnView-Programmdatei wählen");
}

void VideoWidget::openWithFastStone()
{
    openWithViewer("viewer/faststone", "FastStone-Programmdatei wählen");
}

void VideoWidget::openWithViewer(const QString &settingsKey, const QString &title)
{
    QSettings s;
    QString exe = s.value(settingsKey).toString();
    if (exe.isEmpty() || !QFileInfo::exists(exe))
    {
        exe = QFileDialog::getOpenFileName(
            this, title,
            "C:/Program Files",
            "Ausführbare Dateien (*.exe);;Alle Dateien (*)");
        if (exe.isEmpty()) return;
        s.setValue(settingsKey, exe);
    }
    const QString dir = s.value("save/dir").toString();
    if (dir.isEmpty() || !QDir(dir).exists())
    {
        QMessageBox::information(this, "Kein Pfad", "Noch kein Speicherpfad bekannt.");
        return;
    }
    QProcess::startDetached(exe, { dir });
}

const VideoWidget::Sliders VideoWidget::getSliderValues() const
{
    return {m_rangeSlider->lowerValue(),
            m_rangeSlider->upperValue(),
            m_sortSlider->value()};
}
