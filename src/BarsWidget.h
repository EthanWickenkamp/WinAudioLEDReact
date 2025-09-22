#pragma once
#include <QWidget>
#include <QVector>

class BarsWidget : public QWidget {
  Q_OBJECT
public:
  explicit BarsWidget(QWidget* parent = nullptr);

  QSize sizeHint() const override;

public slots:
  void setBinsRawStereo(const QVector<float>& left,
                        const QVector<float>& right);

protected:
  void paintEvent(QPaintEvent* ev) override;

private:
  QVector<float> _left, _right;
};
