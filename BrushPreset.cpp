#include "BrushPreset.h"
#include <QPainter>
#include <QBuffer>
#include <QJsonDocument>

BrushPreset::BrushPreset(int id, const QString &name)
    : m_id(id), m_name(name)
{}

int BrushPreset::widthForPressure(qreal pressure) const
{
    if (!m_usePressure)
        return m_maxWidth;
    qreal p = qBound(0.0, pressure, 1.0);
    return m_minWidth + qRound(p * (m_maxWidth - m_minWidth));
}

void BrushPreset::drawPoint(QPainter *painter, const QPointF &pos, const QColor &color, int width) const
{
    if (width <= 0) width = 1;

    // 如果有纹理且有效，使用纹理绘制
    if (!m_texture.isNull()) {
        // 缩放纹理至目标宽度（保持宽高比）
        QImage scaled = m_texture.scaled(width, width, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (!scaled.isNull() && scaled.width() > 0 && scaled.height() > 0) {
            // 将图片绘制到指定位置（中心对齐）
            QPointF drawPos = pos - QPointF(scaled.width() / 2.0, scaled.height() / 2.0);
            painter->drawImage(drawPos, scaled);
            return;
        }
    }

    // 没有纹理或缩放失败，回退到圆形
    painter->setBrush(color);
    painter->setPen(Qt::NoPen);
    qreal r = width / 2.0;
    painter->drawEllipse(pos, r, r);
}

QJsonObject BrushPreset::toJson() const
{
    QJsonObject obj;
    obj["id"] = m_id;
    obj["name"] = m_name;
    obj["minWidth"] = m_minWidth;
    obj["maxWidth"] = m_maxWidth;
    obj["usePressure"] = m_usePressure;
    if (!m_texture.isNull()) {
        QByteArray ba;
        QBuffer buffer(&ba);
        buffer.open(QIODevice::WriteOnly);
        m_texture.save(&buffer, "PNG");
        obj["texture"] = QString::fromLatin1(ba.toBase64());
    }
    return obj;
}

void BrushPreset::fromJson(const QJsonObject &obj)
{
    m_id = obj["id"].toInt();
    m_name = obj["name"].toString();
    m_minWidth = obj["minWidth"].toInt(1);
    m_maxWidth = obj["maxWidth"].toInt(10);
    m_usePressure = obj["usePressure"].toBool(false);
    if (obj.contains("texture")) {
        QByteArray ba = QByteArray::fromBase64(obj["texture"].toString().toLatin1());
        m_texture.loadFromData(ba, "PNG");
    } else {
        m_texture = QImage();
    }
}