#ifndef BRUSHPRESET_H
#define BRUSHPRESET_H

#include <QObject>
#include <QImage>
#include <QJsonObject>

class BrushPreset
{
public:
    BrushPreset(int id = 0, const QString &name = "默认");
    void setId(int id) { m_id = id; }
    int id() const { return m_id; }
    QString name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }

    int minWidth() const { return m_minWidth; }
    void setMinWidth(int w) { m_minWidth = w; }

    int maxWidth() const { return m_maxWidth; }
    void setMaxWidth(int w) { m_maxWidth = w; }

    bool usePressure() const { return m_usePressure; }
    void setUsePressure(bool on) { m_usePressure = on; }

    QImage texture() const { return m_texture; }
    void setTexture(const QImage &img) { m_texture = img; }

    // 根据压力因子 (0~1) 计算实际宽度
    int widthForPressure(qreal pressure) const;

    // 绘制一个点（位置、颜色、实际宽度）到 painter
    void drawPoint(QPainter *painter, const QPointF &pos, const QColor &color, int width) const;

    // 序列化
    QJsonObject toJson() const;
    void fromJson(const QJsonObject &obj);

private:
    int m_id;
    QString m_name;
    int m_minWidth = 1;
    int m_maxWidth = 10;
    bool m_usePressure = false;
    QImage m_texture;
};

#endif // BRUSHPRESET_H