#include "Label.h"
#include <QMouseEvent>
#include <QPainter>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QRadioButton>
#include <QVBoxLayout>

void Label::mousePressEvent(QMouseEvent *ev)
{
    m_lastPos = ev->pos();
    if (ev->button() != Qt::LeftButton) return;
    origin = ev->pos();
    if (ev->modifiers() & Qt::ControlModifier)
    {
        m_cropState = CropState::Preview;
        m_didDrag   = false;
        QApplication::setOverrideCursor(Qt::BlankCursor);
        update();
        emit cropChanged();
        return;
    }
    if (!rubberBand)
        rubberBand = new QRubberBand(QRubberBand::Rectangle, this);
    m_cropState       = CropState::None;
    // Stand fuer "Abbrechen" im Auswahldialog merken
    m_cropBeforeClick = m_imageCropRect;
    m_padBeforeClick  = m_padRatio;
    // Waehrend des Aufziehens das unveraenderte Bild zeigen, sonst wuerde die
    // Auswahl gegen ein bereits zugeschnittenes/erweitertes Bild gemessen.
    m_imageCropRect = QRect();
    m_padRatio      = QSize();
    emit cropChanged();
    rubberBand->setGeometry(QRect(origin, QSize()));
    rubberBand->show();
}

void Label::mouseMoveEvent(QMouseEvent *ev)
{
    auto deltaPos = m_lastPos - ev->pos();
    m_lastPos = ev->pos();
    if (ev->modifiers() & Qt::ControlModifier)
        m_cropState = CropState::Preview;
    if (m_cropState != CropState::None)
    {
        if (m_newSel.isValid())
        {
            m_newSel.adjust(-deltaPos.x(), -deltaPos.y(), -deltaPos.x(), -deltaPos.y());
            int ox, oy;
            const QPixmap &sc = scale(ox, oy);
            const QRect bounds(ox, oy, sc.width(), sc.height());
            if (m_newSel.left()   < bounds.left())   m_newSel.moveLeft(bounds.left());
            if (m_newSel.top()    < bounds.top())    m_newSel.moveTop(bounds.top());
            if (m_newSel.right()  > bounds.right())  m_newSel.moveRight(bounds.right());
            if (m_newSel.bottom() > bounds.bottom()) m_newSel.moveBottom(bounds.bottom());
            m_cropState = CropState::Dragging;
            m_didDrag   = true;
        }
        else
        {
            m_cropState = CropState::Preview;
        }
        update();
        return;
    }
    if (rubberBand)
        rubberBand->setGeometry(QRect(origin, ev->pos()).normalized());
}

void Label::keyPressEvent(QKeyEvent *ev)
{
    if (QApplication::mouseButtons() & Qt::LeftButton)
        m_cropState = CropState::Preview;
    QLabel::keyPressEvent(ev);
}

void Label::mouseReleaseEvent(QMouseEvent *ev)
{
    if (ev->button() == Qt::RightButton)
    {
        emit rightClicked();
        return;
    }
    if (m_cropState != CropState::None)
    {
        m_cropState = CropState::None;
        QApplication::restoreOverrideCursor();
        if (!m_didDrag)
        {
            m_imageCropRect = QRect();
            m_newSel        = QRect();
            update();
            emit cropChanged();
            return;
        }
    }
    else
    {
        if (!rubberBand) return;
        rubberBand->hide();
        m_newSel = QRect(origin, ev->pos()).normalized();
        if (m_newSel.width() < 8 || m_newSel.height() < 8)
        {
            // Klick ohne Aufziehen: Grundstellung oder festes Seitenverhaeltnis waehlen
            m_newSel = QRect();
            showRatioDialog(mapToGlobal(ev->pos()));
            return;
        }
    }

    QSize s;
    scale(s.rwidth(), s.rheight());
    const QRectF sel = QRectF(m_newSel).adjusted(-s.width(), -s.height(), -s.width(), -s.height());
    const QSize scaledSz = imagePlus.image.size().scaled(size(), Qt::KeepAspectRatio);
    if (!scaledSz.isEmpty())
    {
        const double sw = static_cast<double>(imagePlus.image.width())  / scaledSz.width();
        const double sh = static_cast<double>(imagePlus.image.height()) / scaledSz.height();
        m_imageCropRect = QRect(
            static_cast<int>(sel.x()      * sw),
            static_cast<int>(sel.y()      * sh),
            static_cast<int>(sel.width()  * sw),
            static_cast<int>(sel.height() * sh)
        ).intersected(imagePlus.image.rect());
    }
    update();
    emit cropChanged();
}

// Auswahl nach einem einfachen Linksklick: Grundstellung oder ein festes
// Seitenverhaeltnis, auf das das Bild erweitert wird. [Abbrechen] aendert nichts.
void Label::showRatioDialog(const QPoint &globalPos)
{
    if (imagePlus.image.isNull())
    {
        restoreBeforeClick();
        return;
    }
    struct Entry { const char *text; QSize ratio; };
    const Entry entries[] = {
        { "Grundstellung", QSize()      },
        { "16:9",          QSize(16, 9) },
        { "9:16",          QSize(9, 16) },
        { "4:3",           QSize(4, 3)  },
        { "3:4",           QSize(3, 4)  },
        { "1:1",           QSize(1, 1)  },
    };

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Seitenverhältnis"));
    QVBoxLayout *lay = new QVBoxLayout(&dlg);
    QList<QRadioButton *> buttons;
    for (const Entry &e : entries)
    {
        QRadioButton *rb = new QRadioButton(tr(e.text), &dlg);
        rb->setChecked(e.ratio == m_padRatio);
        buttons << rb;
        lay->addWidget(rb);
    }
    QDialogButtonBox *box = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    lay->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    dlg.move(globalPos);

    if (dlg.exec() != QDialog::Accepted)
    {
        restoreBeforeClick();           // Abbruch: nichts aendern
        return;
    }

    m_imageCropRect = m_cropBeforeClick;   // ein vorhandener Zuschnitt bleibt erhalten
    for (int i = 0; i < buttons.size(); ++i)
    {
        if (!buttons[i]->isChecked())
        {
            continue;
        }
        m_padRatio = entries[i].ratio;
        if (m_padRatio.isEmpty())
        {
            // Grundstellung: weder Zuschnitt noch Erweiterung
            m_imageCropRect = QRect();
            m_newSel        = QRect();
        }
        break;
    }
    update();
    emit cropChanged();
}

// mousePressEvent() loescht Zuschnitt und Erweiterung schon beim Druecken der
// Taste. Wird der Dialog abgebrochen, muss dieser Stand zurueck.
void Label::restoreBeforeClick()
{
    m_imageCropRect = m_cropBeforeClick;
    m_padRatio      = m_padBeforeClick;
    update();
    emit cropChanged();
}

const QPixmap &Label::scale(int &x, int &y)
{
    static QPixmap scaled;
    scaled = imagePlus.image.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    x = (width()  - scaled.width())  / 2;
    y = (height() - scaled.height()) / 2;
    return scaled;
}

void Label::paintEvent(QPaintEvent *e)
{
    if (imagePlus.image.isNull())
    {
        QLabel::paintEvent(e);
        return;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    int x = 0, y = 0;
    const QPixmap &scaled = scale(x, y);
    p.fillRect(rect(), QColor(Qt::blue));
    p.drawPixmap(x, y, scaled);
    p.setPen(QPen(Qt::red, 2));
    p.drawRect(x, y, scaled.width(), scaled.height());
    if (m_cropState != CropState::None && m_newSel.isValid())
    {
        p.setPen(QPen(m_cropState == CropState::Dragging ? Qt::blue : Qt::green, 2));
        p.drawRect(m_newSel.adjusted(2, 2, -2, -2));
    }
}
