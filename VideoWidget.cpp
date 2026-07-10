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
    m_recentMenu   = menuOpen->addMenu("Zuletzt geöffnet/exportiert");
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

    QMenu *menuSave = new QMenu("Speichern", bar);
    bar->addMenu(menuSave);
    menuSave->addAction("Video exportieren", this, &VideoWidget::exportVideo);
    menuSave->addAction("Alles exportieren ", this, &VideoWidget::exportAll);
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
                                  doDropEvent(lastFile());
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

void VideoWidget::exportAll()
{
    if (m_bigMap.isEmpty()) return;
    QSettings s;
nochmal:
    // QString selected;
    QString path;

    QStringList filters = {
        "Ordner (*.)","Files (*.*)"
    };
    // while(true)
    // {
    //     bool done=true; // wenn der Benutzer den speziellen Ordner-Filter auswählt, muss der Dialog mit dem neuen Filter neu geöffnet werden, damit der Ordner-Auswahlmodus aktiviert wird. In diesem Fall soll aber nicht direkt der aktuelle Filter übernommen werden, sondern immer der erste (Ord
    QFileDialog dlg(this, "Video exportieren");
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    dlg.setOption(QFileDialog::DontConfirmOverwrite);
    dlg.setDefaultSuffix("");
    dlg.setDirectory(lastFile());
    dlg.selectFile("video");
    dlg.setLabelText(QFileDialog::Accept, "Accept");
    dlg.setLabelText(QFileDialog::Reject, "Reject");
    // } else
    // {
    //     dlg.setDirectory(s.value("save/dir").toString());
    //     dlg.selectFile(currentBaseName);
    // }
    dlg.setNameFilters(filters);
    // connect(&dlg, &QFileDialog::filterSelected, this, [this, &dlg, &filters, &done](const QString &filter)
    // {
    //     qDebug() << dlg.selectedFiles();
    //     if(int index=filters.indexOf(filter))
    //     {
    //         filters.swapItemsAt(0,index);
    //         dlg.close(); // Filterwechsel → Dialog neu öffnen, damit der spezielle Ordner-Filter oben ist
    //         done = false;
    //     }
    // });
    if (dlg.exec() != QDialog::Accepted)
    {
        qDebug() << "Export abgebrochen" << dlg.result();
        return;
    }
    // if(done)
    // {
    // selected = dlg.selectedNameFilter();
    path = dlg.selectedFiles().value(0);
    //     break;
    // }
    // }

    if (path.isEmpty()) return;
    QString nr("");
    QStringList files;
    QStringList mask;
#define isDir QFileInfo(path).isDir()
#define isDirEmpty QDir(path).isEmpty()
    // if(selected.startsWith("Ordner"))
    // {
    //     mask = {path + "/" + "video%1.mp4", path + "/" + "video%1.gif", path + "/" + "video%1.png" };
    //     if(not isDir)
    //     {
    //         QDir().mkpath(path); // Ordner erstellen, falls er nicht existiert
    //     }
    // } else
    {
        path.remove(QRegularExpression("(_\\d+)?\\.[^.\\\\/]+$")); // beliebige Extension (+ optionales _N) entfernen
        mask = {path + "%1.mp4", path + "%1.gif", path + "%1.png" };
    }
    for(QString &file : mask)
    {   // doppelte namen identifizieren
        while(QFileInfo::exists(QString(file).arg(nr)))
        {
            nr="_"+QString::number(nr.mid(1).toInt()+1);
        }
    }
    for(QString &file : mask)
    {   // filenamen generieren
        files << QString(file).arg(nr);
    }

    switch(QMessageBox::question(this, "Exportiere alle Videos",
                                  QString("Es werden folgende Dateien erstellt:\n\n"
                                          "Video: %1\n"
                                          "GIF:   %2\n"
                                          "Sprite: %3\n\n"
                                          "OK zum Fortfahren, Abbrechen zum Abbrechen, retry fuer neuen Pfad")
                                      .arg(files[0],files[1],files[2]),
                                  QMessageBox::Ok | QMessageBox::Cancel | QMessageBox::Retry))
    {
    case QMessageBox::Ok:
        break;
    case QMessageBox::Retry:
        if(isDir | isDirEmpty)
        {
            QDir(path).rmdir(path); // leeren Ordner entfernen, damit er bei erneutem Dialog wieder auswählbar ist
        }
        goto nochmal;
    default:;
        if(isDir | isDirEmpty)
        {
            QDir(path).rmdir(path); // leeren Ordner entfernen, damit er bei erneutem Dialog wieder auswählbar ist
        }
        return;
    }
    saveVideo(files[0]);
    saveVideo(files[1]);
    saveSpriteSheet(files[2]);
}
void VideoWidget::exportVideo()
{
    if (m_bigMap.isEmpty()) return;

    QAction *senderAct = qobject_cast<QAction*>(sender());
    QSettings s;
    const QString path = QFileDialog::getSaveFileName(
        this, "Video exportieren",
        lastFile(),
        "Video exportieren WEBM (*.webM);;"
        "Video exportieren MP4 (*.mp4);;"
        "Video exportieren GIF (*.gif);;"
        "Video exportieren PNG/LSL (*.png)");
    if (path.isEmpty()) return;
    if (path.endsWith(".png", Qt::CaseInsensitive))
    {
        // PNG export → sprite sheet + LSL
        saveSpriteSheet(path);
        return;
    }
    saveVideo(path);
}

void VideoWidget::saveVideo(const QString &path)
{
    auto sliders = getSliderValues();

    VideoExporter::Options opts;
    opts.fps        = (sliders.step > 0 && m_delay > 0) ? 1000.0 / (m_delay * sliders.step) : 25.0;
    opts.outputPath = path;
    if (path.endsWith(".mp4", Qt::CaseInsensitive))      opts.format = VideoExporter::MP4;
    else if (path.endsWith(".gif", Qt::CaseInsensitive)) opts.format = VideoExporter::GIF;
    else if (path.endsWith(".webM", Qt::CaseInsensitive)) opts.format = VideoExporter::GIF;
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
                addToHistory(QFileInfo(out).absolutePath());
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
    const double fps = m_delay > 0 ? 1000.0 / (m_delay * sliders.step) : 25.0;
    const QPixmap grid = composeGrid(sliders.first, sliders.last - sliders.first + 1, sliders.step);
    if (grid.save(path))
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

    // LSL-Script daneben ablegen
    const QString lslPath = QFileInfo(path).absolutePath() + "/"
                            + QFileInfo(path).baseName() + ".lsl";
    QFile lslFile(lslPath);
    if (lslFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QTextStream ts(&lslFile);
        QString pingPong = m_revers->isChecked() ? " | PING_PONG":  "";
        ts << "// Auto-generated by VideoConverter\n";
        ts << "// Sprite-Sheet: " << m_grid.cols << "x" << m_grid.rows
           << ", " << m_previewList.size() << " frames @ " << QString::number(fps, 'f', 1) << " fps\n";
        ts << "llSetTextureAnim(ANIM_ON | LOOP" << pingPong << ", ALL_SIDES, "
           << m_grid.cols << ", " << m_grid.rows << ", 0, " << m_previewList.size() << ", "
           << QString::number(fps, 'f', 2) << ");\n";
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
    setLastFile(pathAndUrl); // damit es in "Zuletzt geöffnet" auftaucht
    QSettings s;
    QStringList parts = pathAndUrl.split(","); // Pfad und evtl URL trennen
    QStringList list = s.value("history/files").toStringList();
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
