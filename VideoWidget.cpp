#include "VideoWidget.h"
#include "Label.h"
#include "RangeSlider.h"
#include "ComfyBgRemover.h"

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
#include <QFileDialog>
#include <windows.h>
#include <cmath>

// ─── Constructor ────────────────────────────────────────────────────────────

VideoWidget::VideoWidget(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("VLC Frame Grabber");
    QCoreApplication::setOrganizationName("michaelSW");
    QCoreApplication::setOrganizationDomain("uyuni.de");
    QCoreApplication::setApplicationName("FrameGrabber");

    // Menu
    QMenuBar *bar = new QMenuBar(this);
    setMenuBar(bar);
    QMenu *menu = new QMenu("Auflösung", bar);
    bar->addMenu(menu);
    QActionGroup *resGroup = new QActionGroup(this);
    resGroup->setExclusive(true);
    QAction *act1024 = menu->addAction("1024 × 1024");
    act1024->setCheckable(true);
    act1024->setChecked(true);
    resGroup->addAction(act1024);
    connect(act1024, &QAction::triggered, this, [this]{ m_resolution = 1024; });
    QAction *act2048 = menu->addAction("2048 × 2048");
    act2048->setCheckable(true);
    resGroup->addAction(act2048);
    connect(act2048, &QAction::triggered, this, [this]{ m_resolution = 2048; });

    QMenu *menuSave = new QMenu("Speichern", bar);
    bar->addMenu(menuSave);
    QAction *actSaveFrame  = menuSave->addAction("Aktuellen Frame …");
    QAction *actSaveGrid   = menuSave->addAction("Sprite-Sheet …");
    connect(actSaveFrame, &QAction::triggered, this, &VideoWidget::saveCurrentFrame);
    connect(actSaveGrid,  &QAction::triggered, this, &VideoWidget::saveSpriteSheet);

    QMenu *menuEdit = new QMenu("Bearbeiten", bar);
    bar->addMenu(menuEdit);
    QAction *actApplyCrop = menuEdit->addAction(
        "Zuschnitt anwenden  (Pixel-Crop + Zeitbereich + Step)");
    connect(actApplyCrop, &QAction::triggered, this, &VideoWidget::applyCrop);

    QMenu *menuFx = new QMenu("Effekte", bar);
    bar->addMenu(menuFx);

    // Model selection submenu
    QMenu *menuModel = menuFx->addMenu("BiRefNet Modell");
    m_modelGroup = new QActionGroup(this);
    m_modelGroup->setExclusive(true);
    const QStringList models = {
        "BiRefNet-general", "BiRefNet_512x512", "BiRefNet-HR",
        "BiRefNet-portrait", "BiRefNet-matting", "BiRefNet-HR-matting",
        "BiRefNet_lite", "BiRefNet_lite-2K", "BiRefNet_dynamic",
        "BiRefNet_lite-matting", "BiRefNet_toonout"
    };
    for (const QString &m : models) {
        QAction *a = menuModel->addAction(m);
        a->setCheckable(true);
        a->setChecked(m == "BiRefNet-general");
        m_modelGroup->addAction(a);
    }

    menuFx->addSeparator();
    m_actBgRemove = menuFx->addAction("Hintergrund entfernen (ComfyUI)");
    m_actBgRemove->setEnabled(false);
    connect(m_actBgRemove, &QAction::triggered, this, &VideoWidget::startBgRemoval);

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
    m_rangeSlider->blockSignals(true);
    m_sortSlider->blockSignals(true);
    ctrlLayout->addWidget(m_labelLower);
    ctrlLayout->addWidget(m_rangeSlider, 1);
    ctrlLayout->addWidget(m_labelUpper);
    ctrlLayout->addWidget(m_labelSort);
    ctrlLayout->addWidget(m_sortSlider);
    mainLayout->addLayout(ctrlLayout);

    setCentralWidget(central);

    // Connections
    connect(m_label, &Label::rightClicked, this, &VideoWidget::toggleView);
    connect(&m_previewTimer, &QTimer::timeout, this, &VideoWidget::previewTick);
    connect(&gif, &QMovie::frameChanged, this, [this](int) {
        processFrame(gif.currentImage(), gif.currentFrameNumber(), gif.frameCount());
    });
    connect(m_rangeSlider, &RangeSlider::lowerValueChanged, this, &VideoWidget::lowerValueChanged);
    connect(m_rangeSlider, &RangeSlider::upperValueChanged, this, &VideoWidget::upperValueChanged);
    connect(m_sortSlider,  &QSlider::valueChanged,          this, &VideoWidget::sortValueChanged);

    setAcceptDrops(true);
    initVlc();

    QMetaObject::invokeMethod(this, [this] {
        QSettings settings;
        doDropEvent(settings.value("video").toString());
    }, Qt::QueuedConnection);
}

VideoWidget::~VideoWidget()
{
    releaseVlc();
}

// ─── View toggle ────────────────────────────────────────────────────────────

void VideoWidget::toggleView()
{
    if (m_bigMap.isEmpty()) return;
    m_showingGrid = !m_showingGrid;
    m_stack->setCurrentIndex(m_showingGrid ? 1 : 0);
    if (m_showingGrid) {
        paintGrid();
    } else {
        m_previewTimer.stop();
        setWindowTitle("VLC Frame Grabber");
    }
}

bool VideoWidget::eventFilter(QObject *obj, QEvent *event)
{
    if ((obj == m_gridLabel || obj == m_previewLabel)
        && event->type() == QEvent::MouseButtonRelease) {
        if (static_cast<QMouseEvent*>(event)->button() == Qt::RightButton) {
            toggleView();
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

// ─── Grid layout optimisation ────────────────────────────────────────────────

VideoWidget::GridDims VideoWidget::findOptimalGrid(int N, double cropAspect) const
{
    if (N <= 0) return {1, 1};
    if (cropAspect <= 0.0) cropAspect = 1.0;

    int    bestCols = 1, bestRows = N;
    double bestScore = 1e9;

    for (int cols = 1; cols <= N; ++cols) {
        const int baseRows = (N + cols - 1) / cols;
        for (int extra = 0; extra <= 1; ++extra) {          // try +0 and +1 extra row
            const int rows  = baseRows + extra;
            const int waste = cols * rows - N;

            // cell aspect ratio for a square texture
            const double cellAspect = static_cast<double>(rows) / cols;

            // symmetric log-ratio: 0 = perfect, grows for over- and under-stretch
            const double distortion = std::abs(std::log(cellAspect / cropAspect));

            // waste penalty is mild — user accepts +1 row/col
            const double score = distortion + waste * 0.15;

            if (score < bestScore) {
                bestScore = score;
                bestCols  = cols;
                bestRows  = rows;
            }
        }
    }
    return {bestCols, bestRows};
}

double VideoWidget::getCropAspect() const
{
    return m_label->cropAspectRatio();
}

// ─── Grid composing ─────────────────────────────────────────────────────────

QPixmap VideoWidget::composeGrid(int first, int frameCount, int step)
{
    const int N = qMax(1, frameCount / step);
    const double cropAspect = getCropAspect();
    const auto [cols, rows] = findOptimalGrid(N, cropAspect);
    m_gridCols = cols;
    m_gridRows = rows;

    const int cellW = m_resolution / cols;
    const int cellH = m_resolution / rows;

    QPixmap result(m_resolution, m_resolution);
    result.fill(Qt::transparent);
    QPainter p(&result);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    m_previewList.clear();

    for (int i = 0; i < N; ++i) {
        const int key = first + i * step;
        if (!m_bigMap.contains(key)) continue;

        const QPixmap &src = m_bigMap[key];
        m_previewList << src;   // original frame for preview animation

        const int row = i / cols;
        const int col = i % cols;
        const QRect cell(col * cellW, row * cellH, cellW, cellH);

        // Fill cell exactly — no black bars, slight stretch compensated by prim in SL
        p.drawPixmap(cell, src.scaled(cell.size(),
                                      Qt::IgnoreAspectRatio,
                                      Qt::SmoothTransformation));
    }

    // Window title: grid info + stretch factor
    const double cellAspect = static_cast<double>(rows) / cols;
    const double stretch    = (cropAspect > 0.0) ? cellAspect / cropAspect : 1.0;
    const int    waste      = cols * rows - N;
    setWindowTitle(
        QString("Frame Grabber  —  %1×%2  |  %3 frames  |  Stretch %4×  |  Verschnitt %5")
            .arg(cols).arg(rows).arg(N)
            .arg(QString::number(stretch, 'f', 2))
            .arg(waste));

    return result;
}

void VideoWidget::paintGrid()
{
    if (m_bigMap.isEmpty()) return;
    const int first = m_rangeSlider->lowerValue();
    const int last  = m_rangeSlider->upperValue();
    const int step  = m_sortSlider->value();

    const QPixmap grid = composeGrid(first, last - first + 1, step);
    m_gridLabel->setPixmap(grid.scaled(
        m_gridLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

    // Start preview animation
    m_previewIndex = 0;
    if (!m_previewList.isEmpty()) {
        m_previewLabel->setPixmap(m_previewList[0].scaled(
            m_previewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        const int interval = qMax(16, m_delay * step);
        m_previewTimer.start(interval);
    }
}

// ─── Slider slots ───────────────────────────────────────────────────────────

void VideoWidget::lowerValueChanged(int value)
{
    m_labelLower->setText(QString::number(value));
    if (value >= m_rangeSlider->upperValue()) {
        m_rangeSlider->setLowerValue(m_rangeSlider->upperValue() - 1);
        return;
    }
    if (m_showingGrid)
        paintGrid();
    else if (m_bigMap.contains(value)) {
        m_label->setImage(m_bigMap[value], value, m_bigMap.size(), m_delay);
        m_label->update();
    }
}

void VideoWidget::upperValueChanged(int value)
{
    m_labelUpper->setText(QString::number(value));
    if (value <= m_rangeSlider->lowerValue()) {
        m_rangeSlider->setUpperValue(m_rangeSlider->lowerValue() + 1);
        return;
    }
    if (m_showingGrid)
        paintGrid();
    else if (m_bigMap.contains(value)) {
        m_label->setImage(m_bigMap[value], value, m_bigMap.size(), m_delay);
        m_label->update();
    }
}

void VideoWidget::sortValueChanged(int value)
{
    const int maxSort = qMax(1, (m_rangeSlider->upperValue()
                                 - m_rangeSlider->lowerValue() + 1) / 2);
    m_labelSort->setText(QString("%1/%2").arg(value).arg(maxSort));
    if (m_showingGrid)
        paintGrid();
}

// ─── Preview animation ───────────────────────────────────────────────────────

void VideoWidget::previewTick()
{
    if (m_previewList.isEmpty()) return;
    m_previewIndex = (m_previewIndex + 1) % m_previewList.size();

    const int cellW = m_resolution / m_gridCols;
    const int cellH = m_resolution / m_gridRows;

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
    const auto urls = event->mimeData()->urls();
    if (!urls.isEmpty())
        doDropEvent(urls.first().toLocalFile());
}

void VideoWidget::doDropEvent(const QString &path)
{
    if (path.isEmpty()) return;

    gif.stop();
    m_previewTimer.stop();
#if (LIBVLC_VERSION_MAJOR == 4)
    libvlc_media_player_stop_async(m_mediaPlayer);
#else
    libvlc_media_player_stop(m_mediaPlayer);
#endif

    m_label->resetSelAdjList();
    m_bigMap.clear();
    m_previewList.clear();
    m_delay       = 0;
    m_fillingMap  = true;
    m_showingGrid = false;
    m_stack->setCurrentIndex(0);
    m_rangeSlider->blockSignals(true);
    m_sortSlider->blockSignals(true);
    m_labelLower->setText("–");
    m_labelUpper->setText("–");
    m_labelSort->setText("–");
    setWindowTitle("VLC Frame Grabber");

    static const QStringList imageExts = {"png","jpg","jpeg","bmp","tif","tiff","webp"};
    const QString ext = QFileInfo(path).suffix().toLower();

    if (path.endsWith(".gif", Qt::CaseInsensitive)) {
        gif.setFileName(path);
        if (gif.isValid()) { eTime.invalidate(); gif.start(); }
        else return;
    } else if (imageExts.contains(ext)) {
        // Single image → treat as one frame
        QPixmap px(path);
        if (px.isNull()) return;
        m_bigMap[0]   = px;
        m_delay       = 40;
        m_fillingMap  = false;
        m_rangeSlider->setRange(0, 0);
        m_rangeSlider->setLowerValue(0);
        m_rangeSlider->setUpperValue(0);
        m_sortSlider->setRange(1, 1);
        m_sortSlider->setValue(1);
        m_labelLower->setText("0");
        m_labelUpper->setText("0");
        m_labelSort->setText("1/1");
        m_rangeSlider->blockSignals(false);
        m_sortSlider->blockSignals(false);
        m_actBgRemove->setEnabled(true);
        m_label->setImage(px, 0, 1, m_delay);
        m_label->update();
    } else {
        frame = Frame();
        eTime.invalidate();
        if (!playFile(path)) return;
    }
    QSettings settings;
    settings.setValue("video", path);
}

// ─── Frame processing ────────────────────────────────────────────────────────

void VideoWidget::processFrame(const QImage &img, int index, int count)
{
    if (!eTime.isValid()) { eTime.start(); return; }

    const int elapsed = static_cast<int>(eTime.elapsed());
    eTime.restart();

    const QPixmap px = QPixmap::fromImage(img);
    m_label->setImage(px, index, count, elapsed);
    m_label->update();

    if (m_fillingMap && count > 0) {
        m_delay += elapsed;
        m_bigMap[index] = px;

        if (m_bigMap.size() == count) {
            m_delay /= count;

            m_rangeSlider->setRange(0, count - 1);
            m_rangeSlider->setLowerValue(0);
            m_rangeSlider->setUpperValue(count - 1);

            const int maxSort = qMax(1, count / 2);
            m_sortSlider->setRange(1, maxSort);
            m_sortSlider->setValue(1);

            m_labelLower->setText("0");
            m_labelUpper->setText(QString::number(count - 1));
            m_labelSort->setText(QString("1/%1").arg(maxSort));

            m_rangeSlider->blockSignals(false);
            m_sortSlider->blockSignals(false);
            m_fillingMap = false;
            m_actBgRemove->setEnabled(true);
        }
    }
}

bool VideoWidget::playFile(const QString &path)
{
    if (!m_vlcInstance || !m_mediaPlayer) { qWarning() << "VLC not init"; return false; }
    if (m_media) { libvlc_media_release(m_media); m_media = nullptr; }

    QByteArray ba = QDir::toNativeSeparators(path).toUtf8();
#if (LIBVLC_VERSION_MAJOR == 4)
    m_media = libvlc_media_new_path(ba.constData());
#else
    m_media = libvlc_media_new_path(m_vlcInstance, ba.constData());
#endif
    if (!m_media) { qWarning() << "Cannot load:" << path; return false; }

    libvlc_media_player_set_media(m_mediaPlayer, m_media);
    libvlc_media_player_play(m_mediaPlayer);
    return true;
}

// ─── VLC init / release ──────────────────────────────────────────────────────

void VideoWidget::initVlc()
{
    wchar_t path[256];
    QString dllPath(QDir::toNativeSeparators(VLC_SDK_PATH "/lib"));
    dllPath.toWCharArray(path);
    qDebug() << dllPath << " : " << SetDllDirectoryW(path);

    m_vlcInstance = libvlc_new(0, nullptr);
    if (!m_vlcInstance) { qWarning() << "libVLC init failed"; return; }

    m_mediaPlayer = libvlc_media_player_new(m_vlcInstance);
    if (!m_mediaPlayer) { qWarning() << "Media player create failed"; return; }

    libvlc_video_set_callbacks(m_mediaPlayer,
        &VideoWidget::lockCallback,
        &VideoWidget::unlockCallback,
        &VideoWidget::displayCallback, this);

    libvlc_video_set_format_callbacks(m_mediaPlayer,
        &VideoWidget::formatCallback,
        &VideoWidget::formatCleanupCallback);

    libvlc_event_manager_t *em = libvlc_media_player_event_manager(m_mediaPlayer);
    libvlc_event_attach(em, libvlc_MediaPlayerEndReached,
        [](const libvlc_event_t*, void *d) {
            auto self = static_cast<VideoWidget*>(d);
            QMetaObject::invokeMethod(self, [self] {
                self->frame.count   = self->frame.current;
                self->frame.current = 0;
                libvlc_media_player_stop(self->m_mediaPlayer);
                libvlc_media_player_play(self->m_mediaPlayer);
            });
        }, this);
}

void VideoWidget::releaseVlc()
{
    if (m_mediaPlayer) {
#if (LIBVLC_VERSION_MAJOR == 4)
        libvlc_media_player_stop_async(m_mediaPlayer);
#else
        libvlc_media_player_stop(m_mediaPlayer);
#endif
        libvlc_media_player_release(m_mediaPlayer);
        m_mediaPlayer = nullptr;
    }
    if (m_media)        { libvlc_media_release(m_media);       m_media        = nullptr; }
    if (m_vlcInstance)  { libvlc_release(m_vlcInstance);       m_vlcInstance  = nullptr; }
}

// ─── VLC callbacks ───────────────────────────────────────────────────────────

void *VideoWidget::lockCallback(void *opaque, void **planes)
{
    VideoWidget *self = static_cast<VideoWidget*>(opaque);
    self->m_frameMutex.lock();
    *planes = self->frame.image.bits();
    return nullptr;
}

void VideoWidget::unlockCallback(void *opaque, void *picture, void *const *planes)
{
    Q_UNUSED(picture); Q_UNUSED(planes);
    static_cast<VideoWidget*>(opaque)->m_frameMutex.unlock();
}

void VideoWidget::displayCallback(void *opaque, void *picture)
{
    Q_UNUSED(picture);
    VideoWidget *self = static_cast<VideoWidget*>(opaque);

    QImage imageCopy;
    int index, count;
    {
        QMutexLocker locker(&self->m_frameMutex);
        imageCopy = self->frame.image.copy();
        index     = self->frame.current++;
        count     = self->frame.count;
    }

    QMetaObject::invokeMethod(self,
        [self, img = std::move(imageCopy), index, count]() mutable {
            self->processFrame(img, index, count);
        });
}

unsigned VideoWidget::formatCallback(void **opaque, char *chroma,
                                     unsigned *width, unsigned *height,
                                     unsigned *pitches, unsigned *lines)
{
    VideoWidget *self = static_cast<VideoWidget*>(*opaque);
#if (LIBVLC_VERSION_MAJOR == 4)
    chroma[0]='B'; chroma[1]='G'; chroma[2]='R'; chroma[3]='A';
#else
    chroma[0]='R'; chroma[1]='V'; chroma[2]='3'; chroma[3]='2';
#endif
    self->m_videoWidth  = static_cast<int>(*width);
    self->m_videoHeight = static_cast<int>(*height);
    self->m_pitch       = static_cast<int>(*width) * 4;
    *pitches = self->m_pitch;
    *lines   = self->m_videoHeight;
    {
        QMutexLocker locker(&self->m_frameMutex);
        self->frame.newImage(self->m_videoWidth, self->m_videoHeight);
    }
    return 1;
}

void VideoWidget::formatCleanupCallback(void *) {}

// ─── Background removal ───────────────────────────────────────────────────────

void VideoWidget::startBgRemoval()
{
    if (m_bigMap.isEmpty()) return;

    // Only process frames in current slider range with step
    const int first = m_rangeSlider->lowerValue();
    const int last  = m_rangeSlider->upperValue();
    const int step  = m_sortSlider->value();
    QMap<int, QPixmap> toProcess;
    for (int i = first; i <= last; i += step)
        if (m_bigMap.contains(i))
            toProcess[i] = m_bigMap[i];
    if (toProcess.isEmpty()) return;

    m_actBgRemove->setEnabled(false);
    setWindowTitle(QString("Hintergrund wird entfernt … (0/%1)").arg(toProcess.size()));

    m_bgRemover = new ComfyBgRemover("http://127.0.0.1:8188", this);
    connect(m_bgRemover, &ComfyBgRemover::frameReady, this, &VideoWidget::onBgFrameReady);
    connect(m_bgRemover, &ComfyBgRemover::progress,   this, &VideoWidget::onBgProgress);
    connect(m_bgRemover, &ComfyBgRemover::finished,   this, &VideoWidget::onBgFinished);
    connect(m_bgRemover, &ComfyBgRemover::error, this, [this](const QString &msg) {
        setWindowTitle("Fehler: " + msg);
        m_actBgRemove->setEnabled(true);
        m_bgRemover->deleteLater();
        m_bgRemover = nullptr;
    });

    const QAction *checked = m_modelGroup->checkedAction();
    const QString model = checked ? checked->text() : "BiRefNet-general";
    m_bgRemover->process(toProcess, model);
}

void VideoWidget::onBgFrameReady(int index, QPixmap result)
{
    m_bigMap[index] = result;
    // Show current frame live if it's visible
    if (!m_showingGrid && m_label->currentIndex() == index) {
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
    setWindowTitle("VLC Frame Grabber");
    m_actBgRemove->setEnabled(true);
    m_bgRemover->deleteLater();
    m_bgRemover = nullptr;
    if (m_showingGrid)
        paintGrid();
    else {
        const int cur = m_rangeSlider->lowerValue();
        if (m_bigMap.contains(cur)) {
            m_label->setImage(m_bigMap[cur], cur, m_bigMap.size(), m_delay);
            m_label->update();
        }
    }
}

// ─── Speichern ────────────────────────────────────────────────────────────────

void VideoWidget::saveCurrentFrame()
{
    if (m_bigMap.isEmpty()) return;
    const int idx = m_rangeSlider->lowerValue();
    if (!m_bigMap.contains(idx)) return;

    const QString path = QFileDialog::getSaveFileName(
        this, "Frame speichern", QString(), "PNG (*.png);;All files (*)");
    if (path.isEmpty()) return;

    m_bigMap[idx].save(path, "PNG");
}

void VideoWidget::applyCrop()
{
    if (m_bigMap.isEmpty()) return;

    const QRect cropRect = m_label->cropRectInImageCoords();
    const int first = m_rangeSlider->lowerValue();
    const int last  = m_rangeSlider->upperValue();
    const int step  = m_sortSlider->value();

    QMap<int, QPixmap> newMap;
    int newIndex = 0;
    for (int i = first; i <= last; i += step) {
        if (!m_bigMap.contains(i)) continue;
        QPixmap px = m_bigMap[i];
        if (!cropRect.isEmpty() && cropRect != px.rect())
            px = px.copy(cropRect);
        newMap[newIndex++] = px;
    }
    if (newMap.isEmpty()) return;

    m_bigMap = newMap;
    m_delay  = qMax(16, m_delay * step);

    const int count = m_bigMap.size();
    m_rangeSlider->blockSignals(true);
    m_sortSlider->blockSignals(true);
    m_rangeSlider->setRange(0, count - 1);
    m_rangeSlider->setLowerValue(0);
    m_rangeSlider->setUpperValue(count - 1);
    const int maxSort = qMax(1, count / 2);
    m_sortSlider->setRange(1, maxSort);
    m_sortSlider->setValue(1);
    m_labelLower->setText("0");
    m_labelUpper->setText(QString::number(count - 1));
    m_labelSort->setText(QString("1/%1").arg(maxSort));
    m_rangeSlider->blockSignals(false);
    m_sortSlider->blockSignals(false);

    m_label->resetSelAdjList();
    m_label->setImage(m_bigMap[0], 0, count, m_delay);
    m_label->update();

    if (m_showingGrid) paintGrid();
}

void VideoWidget::saveSpriteSheet()
{
    if (m_bigMap.isEmpty()) return;

    const QString path = QFileDialog::getSaveFileName(
        this, "Sprite-Sheet speichern",
        QString("%1x%2_%3frames.png")
            .arg(m_resolution).arg(m_resolution)
            .arg(m_rangeSlider->upperValue() - m_rangeSlider->lowerValue() + 1),
        "PNG (*.png);;All files (*)");
    if (path.isEmpty()) return;

    const QPixmap grid = composeGrid(
        m_rangeSlider->lowerValue(),
        m_rangeSlider->upperValue() - m_rangeSlider->lowerValue() + 1,
        m_sortSlider->value());

    grid.save(path, "PNG");
}
