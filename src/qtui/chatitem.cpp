/***************************************************************************
 *   Copyright (C) 2005-08 by the Quassel Project                          *
 *   devel@quassel-irc.org                                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) version 3.                                           *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFontMetrics>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QTextLayout>
#include <QMenu>

#include "buffermodel.h"
#include "bufferview.h"
#include "chatitem.h"
#include "chatlinemodel.h"
#include "iconloader.h"
#include "mainwin.h"
#include "qtui.h"
#include "qtuistyle.h"

ChatItem::ChatItem(const qreal &width, const qreal &height, const QPointF &pos, QGraphicsItem *parent)
  : QGraphicsItem(parent),
    _data(0),
    _boundingRect(0, 0, width, height),
    _selectionMode(NoSelection),
    _selectionStart(-1)
{
  setAcceptHoverEvents(true);
  setZValue(20);
  setPos(pos);
}

ChatItem::~ChatItem() {
  delete _data;
}

QVariant ChatItem::data(int role) const {
  QModelIndex index = model()->index(row(), column());
  if(!index.isValid()) {
    qWarning() << "ChatItem::data(): model index is invalid!" << index;
    return QVariant();
  }
  return model()->data(index, role);
}

QTextLayout *ChatItem::createLayout(QTextOption::WrapMode wrapMode, Qt::Alignment alignment) const {
  QTextLayout *layout = new QTextLayout(data(MessageModel::DisplayRole).toString());

  QTextOption option;
  option.setWrapMode(wrapMode);
  option.setAlignment(alignment);
  layout->setTextOption(option);

  QList<QTextLayout::FormatRange> formatRanges
         = QtUi::style()->toTextLayoutList(data(MessageModel::FormatRole).value<UiStyle::FormatList>(), layout->text().length());
  layout->setAdditionalFormats(formatRanges);
  return layout;
}

void ChatItem::doLayout() {
  QTextLayout *layout_ = layout();
  layout_->beginLayout();
  QTextLine line = layout_->createLine();
  if(line.isValid()) {
    line.setLineWidth(width());
    line.setPosition(QPointF(0,0));
  }
  layout_->endLayout();
}

void ChatItem::clearLayout() {
  delete _data;
  _data = 0;
}

ChatItemPrivate *ChatItem::privateData() const {
  if(!_data) {
    ChatItem *that = const_cast<ChatItem *>(this);
    that->_data = that->newPrivateData();
    that->doLayout();
  }
  return _data;
}

// NOTE: This is not the most time-efficient implementation, but it saves space by not caching unnecessary data
//       This is a deliberate trade-off. (-> selectFmt creation, data() call)
void ChatItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
  Q_UNUSED(option); Q_UNUSED(widget);
  painter->setClipRect(boundingRect()); // no idea why QGraphicsItem clipping won't work
  QVector<QTextLayout::FormatRange> formats = additionalFormats();
  QTextLayout::FormatRange selectFmt = selectionFormat();
  if(selectFmt.format.isValid()) formats.append(selectFmt);
  layout()->draw(painter, QPointF(0,0), formats, boundingRect());

  // Debuging Stuff
  // uncomment partially or all of the following stuff:
  //
  // 0) alternativ painter color for debug stuff
//   if(row() % 2)
//     painter->setPen(Qt::red);
//   else
//     painter->setPen(Qt::blue);
  // 1) draw wordwrap points in the first line
//   if(column() == 2) {
//     ChatLineModel::WrapList wrapList = data(ChatLineModel::WrapListRole).value<ChatLineModel::WrapList>();
//     foreach(ChatLineModel::Word word, wrapList) {
//       if(word.endX > width())
// 	break;
//       painter->drawLine(word.endX, 0, word.endX, height());
//     }
//   }
  // 2) draw MsgId over the time column
//   if(column() == 0) {
//     QString msgIdString = QString::number(data(MessageModel::MsgIdRole).value<MsgId>().toInt());
//     QPointF bottomPoint = boundingRect().bottomLeft();
//     bottomPoint.ry() -= 2;
//     painter->drawText(bottomPoint, msgIdString);
//   }
  // 3) draw bounding rect
//   painter->drawRect(_boundingRect.adjusted(0, 0, -1, -1));
}

qint16 ChatItem::posToCursor(const QPointF &pos) const {
  if(pos.y() > height()) return data(MessageModel::DisplayRole).toString().length();
  if(pos.y() < 0) return 0;
  for(int l = layout()->lineCount() - 1; l >= 0; l--) {
    QTextLine line = layout()->lineAt(l);
    if(pos.y() >= line.y()) {
      return line.xToCursor(pos.x(), QTextLine::CursorOnCharacter);
    }
  }
  return 0;
}

bool ChatItem::hasSelection() const {
  if(_selectionMode == NoSelection)
    return false;
  if(_selectionMode == FullSelection)
    return true;
  // partial
  return _selectionStart != _selectionEnd;
}

QString ChatItem::selection() const {
  if(_selectionMode == FullSelection)
    return data(MessageModel::DisplayRole).toString();
  if(_selectionMode == PartialSelection)
    return data(MessageModel::DisplayRole).toString().mid(qMin(_selectionStart, _selectionEnd), qAbs(_selectionStart - _selectionEnd));
  return QString();
}

void ChatItem::setSelection(SelectionMode mode, qint16 start, qint16 end) {
  _selectionMode = mode;
  _selectionStart = start;
  _selectionEnd = end;
  update();
}

void ChatItem::setFullSelection() {
  if(_selectionMode != FullSelection) {
    _selectionMode = FullSelection;
    update();
  }
}

void ChatItem::clearSelection() {
  if(_selectionMode != NoSelection) {
    _selectionMode = NoSelection;
    update();
  }
}

void ChatItem::continueSelecting(const QPointF &pos) {
  _selectionMode = PartialSelection;
  _selectionEnd = posToCursor(pos);
  update();
}

bool ChatItem::isPosOverSelection(const QPointF &pos) const {
  if(_selectionMode == FullSelection)
    return true;
  if(_selectionMode == PartialSelection) {
    int cursor = posToCursor(pos);
    return cursor >= qMin(_selectionStart, _selectionEnd) && cursor <= qMax(_selectionStart, _selectionEnd);
  }
  return false;
}

QTextLayout::FormatRange ChatItem::selectionFormat() const {
  QTextLayout::FormatRange selectFmt;
  if(_selectionMode != NoSelection) {
    selectFmt.format.setForeground(QApplication::palette().brush(QPalette::HighlightedText));
    selectFmt.format.setBackground(QApplication::palette().brush(QPalette::Highlight));
    if(_selectionMode == PartialSelection) {
      selectFmt.start = qMin(_selectionStart, _selectionEnd);
      selectFmt.length = qAbs(_selectionStart - _selectionEnd);
    } else { // FullSelection
      selectFmt.start = 0;
      selectFmt.length = data(MessageModel::DisplayRole).toString().length();
    }
  } else {
    selectFmt.start = -1;
    selectFmt.length = 0;
  }
  return selectFmt;
}

QList<QRectF> ChatItem::findWords(const QString &searchWord, Qt::CaseSensitivity caseSensitive) {
  QList<QRectF> resultList;
  const QAbstractItemModel *model_ = model();
  if(!model_)
    return resultList;

  QString plainText = model_->data(model_->index(row(), column()), MessageModel::DisplayRole).toString();
  QList<int> indexList;
  int searchIdx = plainText.indexOf(searchWord, 0, caseSensitive);
  while(searchIdx != -1) {
    indexList << searchIdx;
    searchIdx = plainText.indexOf(searchWord, searchIdx + 1, caseSensitive);
  }

  bool hadPrivateData = hasPrivateData();

  foreach(int idx, indexList) {
    QTextLine line = layout()->lineForTextPosition(idx);
    qreal x = line.cursorToX(idx);
    qreal width = line.cursorToX(idx + searchWord.count()) - x;
    qreal height = line.height();
    qreal y = height * line.lineNumber();
    resultList << QRectF(x, y, width, height);
  }

  if(!hadPrivateData)
    clearLayout();
  return resultList;
}

void ChatItem::handleClick(const QPointF &pos, ChatScene::ClickMode clickMode) {
  // single clicks are already handled by the scene (for clearing the selection)
  if(clickMode == ChatScene::DragStartClick) {
    chatScene()->setSelectingItem(this);
    _selectionStart = _selectionEnd = posToCursor(pos);
    _selectionMode = NoSelection; // will be set to PartialSelection by mouseMoveEvent
    update();
  }
}

void ChatItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event) {
  if(event->buttons() == Qt::LeftButton) {
    if(contains(event->pos())) {
      qint16 end = posToCursor(event->pos());
      if(end != _selectionEnd) {
        _selectionEnd = end;
        _selectionMode = (_selectionStart != _selectionEnd ? PartialSelection : NoSelection);
        update();
      }
    } else {
      setFullSelection();
      chatScene()->startGlobalSelection(this, event->pos());
    }
    event->accept();
  } else {
    event->ignore();
  }
}

void ChatItem::mousePressEvent(QGraphicsSceneMouseEvent *event) {
  if(event->buttons() == Qt::LeftButton)
    event->accept();
  else
    event->ignore();
}

void ChatItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
  if(_selectionMode != NoSelection && !event->buttons() & Qt::LeftButton) {
    chatScene()->selectionToClipboard(QClipboard::Selection);
    event->accept();
  } else
    event->ignore();
}

void ChatItem::addActionsToMenu(QMenu *menu, const QPointF &pos) {
  Q_UNUSED(menu);
  Q_UNUSED(pos);

}

// ************************************************************
// SenderChatItem
// ************************************************************

void SenderChatItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
  Q_UNUSED(option); Q_UNUSED(widget);

  painter->setClipRect(boundingRect()); // no idea why QGraphicsItem clipping won't work
  qreal layoutWidth = layout()->minimumWidth();
  qreal offset = 0;
  if(chatScene()->senderCutoffMode() == ChatScene::CutoffLeft)
    offset = qMin(width() - layoutWidth, (qreal)0);
  else
    offset = qMax(layoutWidth - width(), (qreal)0);

  QTextLayout::FormatRange selectFmt = selectionFormat();

  if(layoutWidth > width()) {
    // Draw a nice gradient for longer items
    // Qt's text drawing with a gradient brush sucks, so we use an alpha-channeled pixmap instead
    QPixmap pixmap(layout()->boundingRect().toRect().size());
    pixmap.fill(Qt::transparent);
    QPainter pixPainter(&pixmap);
    layout()->draw(&pixPainter, QPointF(qMax(offset, (qreal)0), 0), QVector<QTextLayout::FormatRange>() << selectFmt);
    pixPainter.end();

    // Create alpha channel mask
    QPixmap mask(pixmap.size());
    QPainter maskPainter(&mask);
    QLinearGradient gradient;
    if(offset < 0) {
      gradient.setStart(0, 0);
      gradient.setFinalStop(12, 0);
      gradient.setColorAt(0, Qt::black);
      gradient.setColorAt(1, Qt::white);
    } else {
      gradient.setStart(width()-10, 0);
      gradient.setFinalStop(width(), 0);
      gradient.setColorAt(0, Qt::white);
      gradient.setColorAt(1, Qt::black);
    }
    maskPainter.fillRect(boundingRect(), gradient);
    pixmap.setAlphaChannel(mask);
    painter->drawPixmap(0, 0, pixmap);
  } else {
    layout()->draw(painter, QPointF(0,0), QVector<QTextLayout::FormatRange>() << selectFmt, boundingRect());
  }
}

// ************************************************************
// ContentsChatItem
// ************************************************************

ContentsChatItem::ActionProxy ContentsChatItem::_actionProxy;

ContentsChatItem::ContentsChatItem(const qreal &width, const QPointF &pos, QGraphicsItem *parent)
  : ChatItem(0, 0, pos, parent)
{
  const QAbstractItemModel *model_ = model();
  QModelIndex index = model_->index(row(), column());
  _fontMetrics = QtUi::style()->fontMetrics(model_->data(index, ChatLineModel::FormatRole).value<UiStyle::FormatList>().at(0).second);

  setGeometryByWidth(width);
}

qreal ContentsChatItem::setGeometryByWidth(qreal w) {
  if(w != width()) {
    prepareGeometryChange();
    setWidth(w);
    // compute height
    int lines = 1;
    WrapColumnFinder finder(this);
    while(finder.nextWrapColumn() > 0)
      lines++;
    setHeight(lines * fontMetrics()->lineSpacing());
  }
  return height();
}

void ContentsChatItem::doLayout() {
  ChatLineModel::WrapList wrapList = data(ChatLineModel::WrapListRole).value<ChatLineModel::WrapList>();
  if(!wrapList.count()) return; // empty chatitem

  qreal h = 0;
  WrapColumnFinder finder(this);
  layout()->beginLayout();
  forever {
    QTextLine line = layout()->createLine();
    if(!line.isValid())
      break;

    int col = finder.nextWrapColumn();
    line.setNumColumns(col >= 0 ? col - line.textStart() : layout()->text().length());
    line.setPosition(QPointF(0, h));
    h += fontMetrics()->lineSpacing();
  }
  layout()->endLayout();
}

// NOTE: This method is not threadsafe and not reentrant!
//       (RegExps are not constant while matching, and they are static here for efficiency)
QList<ContentsChatItem::Clickable> ContentsChatItem::findClickables() const {
  // For matching URLs
  static QString urlEnd("(?:>|[,.;:\"]*\\s|\\b|$)");
  static QString urlChars("(?:[,.;:]*[\\w\\-~@/?&=+$()!%#*|{}\\[\\]])");

  static QRegExp regExp[] = {
    // URL
    // QRegExp(QString("((?:https?://|s?ftp://|irc://|mailto:|www\\.)%1+|%1+\\.[a-z]{2,4}(?:?=/%1+|\\b))%2").arg(urlChars, urlEnd)),
    QRegExp(QString("((?:(?:https?://|s?ftp://|irc://|gopher://|mailto:)|www)%1+)%2").arg(urlChars, urlEnd), Qt::CaseInsensitive),

    // Channel name
    // We don't match for channel names starting with + or &, because that gives us a lot of false positives.
    QRegExp("((?:#|![A-Z0-9]{5})[^,:\\s]+(?::[^,:\\s]+)?)\\b", Qt::CaseInsensitive)

    // TODO: Nicks, we'll need a filtering for only matching known nicknames further down if we do this
  };

  static const int regExpCount = 2;  // number of regexps in the array above

  qint16 matches[] = { 0, 0, 0 };
  qint16 matchEnd[] = { 0, 0, 0 };

  QString str = data(ChatLineModel::DisplayRole).toString();

  QList<Clickable> result;
  qint16 idx = 0;
  qint16 minidx;
  int type = -1;

  do {
    type = -1;
    minidx = str.length();
    for(int i = 0; i < regExpCount; i++) {
      if(matches[i] < 0 || matchEnd[i] > str.length()) continue;
      if(idx >= matchEnd[i]) {
        matches[i] = str.indexOf(regExp[i], qMax(matchEnd[i], idx));
        if(matches[i] >= 0) matchEnd[i] = matches[i] + regExp[i].cap(1).length();
      }
      if(matches[i] >= 0 && matches[i] < minidx) {
        minidx = matches[i];
        type = i;
      }
    }
    if(type >= 0) {
      idx = matchEnd[type];
      if(type == Clickable::Url && str.at(idx-1) == ')') {  // special case: closing paren only matches if we had an open one
        if(!str.mid(matches[type], matchEnd[type]-matches[type]).contains('(')) matchEnd[type]--;
      }
      result.append(Clickable((Clickable::Type)type, matches[type], matchEnd[type] - matches[type]));
    }
  } while(type >= 0);

  /* testing
  if(!result.isEmpty()) qDebug() << str;
  foreach(Clickable click, result) {
    qDebug() << str.mid(click.start, click.length);
  }
  */
  return result;
}

ContentsChatItem::Clickable ContentsChatItem::clickableAt(const QPointF &pos) const {
  qint16 idx = posToCursor(pos);
  for(int i = 0; i < privateData()->clickables.count(); i++) {
    Clickable click = privateData()->clickables.at(i);
    if(idx >= click.start && idx < click.start + click.length)
      return click;
  }
  return Clickable();
}

QVector<QTextLayout::FormatRange> ContentsChatItem::additionalFormats() const {
  // mark a clickable if hovered upon
  QVector<QTextLayout::FormatRange> fmt;
  if(privateData()->currentClickable.isValid()) {
    Clickable click = privateData()->currentClickable;
    QTextLayout::FormatRange f;
    f.start = click.start;
    f.length = click.length;
    f.format.setFontUnderline(true);
    fmt.append(f);
  }
  return fmt;
}

void ContentsChatItem::endHoverMode() {
  if(hasPrivateData()) {
    if(privateData()->currentClickable.isValid()) {
      setCursor(Qt::ArrowCursor);
      privateData()->currentClickable = Clickable();
    }
    clearWebPreview();
    update();
  }
}

void ContentsChatItem::handleClick(const QPointF &pos, ChatScene::ClickMode clickMode) {
  if(clickMode == ChatScene::SingleClick) {
    Clickable click = clickableAt(pos);
    if(click.isValid()) {
      QString str = data(ChatLineModel::DisplayRole).toString().mid(click.start, click.length);
      switch(click.type) {
        case Clickable::Url:
          if(!str.contains("://"))
            str = "http://" + str;
          QDesktopServices::openUrl(QUrl::fromEncoded(str.toUtf8(), QUrl::TolerantMode));
          break;
        case Clickable::Channel: {
          NetworkId networkId = Client::networkModel()->networkId(data(MessageModel::BufferIdRole).value<BufferId>());
          BufferId bufId = Client::networkModel()->bufferId(networkId, str);
          if(bufId.isValid()) {
            QModelIndex targetIdx = Client::networkModel()->bufferIndex(bufId);
            Client::bufferModel()->switchToBuffer(bufId);
            if(!targetIdx.data(NetworkModel::ItemActiveRole).toBool())
              Client::userInput(BufferInfo::fakeStatusBuffer(networkId), QString("/JOIN %1").arg(str));
          } else
              Client::userInput(BufferInfo::fakeStatusBuffer(networkId), QString("/JOIN %1").arg(str));
          break;
        }
        default:
          break;
      }
    }
  } else if(clickMode == ChatScene::DoubleClick) {
    chatScene()->setSelectingItem(this);
    setSelectionMode(PartialSelection);
    Clickable click = clickableAt(pos);
    if(click.isValid()) {
      setSelectionStart(click.start);
      setSelectionEnd(click.start + click.length);
    } else {
      // find word boundary
      QString str = data(ChatLineModel::DisplayRole).toString();
      qint16 cursor = posToCursor(pos);
      qint16 start = str.lastIndexOf(QRegExp("\\W"), cursor) + 1;
      qint16 end = qMin(str.indexOf(QRegExp("\\W"), cursor), str.length());
      if(end < 0) end = str.length();
      setSelectionStart(start);
      setSelectionEnd(end);
    }
    update();
  } else if(clickMode == ChatScene::TripleClick) {
    setSelection(PartialSelection, 0, data(ChatLineModel::DisplayRole).toString().length());
  }
  ChatItem::handleClick(pos, clickMode);
}

void ContentsChatItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event) {
  // mouse move events always mean we're not hovering anymore...
  endHoverMode();
  ChatItem::mouseMoveEvent(event);
}

void ContentsChatItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event) {
  endHoverMode();
  event->accept();
}

void ContentsChatItem::hoverMoveEvent(QGraphicsSceneHoverEvent *event) {
  bool onClickable = false;
  Clickable click = clickableAt(event->pos());
  if(click.isValid()) {
    if(click.type == Clickable::Url) {
      onClickable = true;
      showWebPreview(click);
    } else if(click.type == Clickable::Channel) {
      // don't make clickable if it's our own name
      QString name = data(ChatLineModel::DisplayRole).toString().mid(click.start, click.length);
      BufferId myId = data(MessageModel::BufferIdRole).value<BufferId>();
      if(Client::networkModel()->bufferName(myId) != name)
        onClickable = true;
    }
    if(onClickable) {
      setCursor(Qt::PointingHandCursor);
      privateData()->currentClickable = click;
      update();
      return;
    }
  }
  if(!onClickable) endHoverMode();
  event->accept();
}

void ContentsChatItem::addActionsToMenu(QMenu *menu, const QPointF &pos) {
  Q_UNUSED(pos); // we assume that the current mouse cursor pos is the point of invocation

  if(privateData()->currentClickable.isValid()) {
    Clickable click = privateData()->currentClickable;
    switch(click.type) {
      case Clickable::Url:
        privateData()->activeClickable = click;
        menu->addAction(SmallIcon("edit-copy"), tr("Copy Link Address"),
                         &_actionProxy, SLOT(copyLinkToClipboard()))->setData(QVariant::fromValue<void *>(this));
        break;
      case Clickable::Channel: {
        // Hide existing menu actions, they confuse us when right-clicking on a clickable
        foreach(QAction *action, menu->actions())
          action->setVisible(false);
        QString name = data(ChatLineModel::DisplayRole).toString().mid(click.start, click.length);
        Client::mainUi()->actionProvider()->addActions(menu, chatScene()->filter(), data(MessageModel::BufferIdRole).value<BufferId>(), name);
        break;
      }
      default:
        break;
    }
  } else {

    // Buffer-specific actions
    Client::mainUi()->actionProvider()->addActions(menu, chatScene()->filter(), data(MessageModel::BufferIdRole).value<BufferId>());
  }
}

void ContentsChatItem::copyLinkToClipboard() {
  Clickable click = privateData()->activeClickable;
  if(click.isValid() && click.type == Clickable::Url) {
    QString url = data(ChatLineModel::DisplayRole).toString().mid(click.start, click.length);
    if(!url.contains("://"))
      url = "http://" + url;
    chatScene()->stringToClipboard(url);
  }
}

/******** WEB PREVIEW *****************************************************************************/

void ContentsChatItem::showWebPreview(const Clickable &click) {
#ifndef HAVE_WEBKIT
  Q_UNUSED(click);
#else
  QTextLine line = layout()->lineForTextPosition(click.start);
  qreal x = line.cursorToX(click.start);
  qreal width = line.cursorToX(click.start + click.length) - x;
  qreal height = line.height();
  qreal y = height * line.lineNumber();

  QPointF topLeft = scenePos() + QPointF(x, y);
  QRectF urlRect = QRectF(topLeft.x(), topLeft.y(), width, height);

  QString url = data(ChatLineModel::DisplayRole).toString().mid(click.start, click.length);
  if(!url.contains("://"))
    url = "http://" + url;
  chatScene()->loadWebPreview(this, url, urlRect);
#endif
}

void ContentsChatItem::clearWebPreview() {
#ifdef HAVE_WEBKIT
  chatScene()->clearWebPreview(this);
#endif
}

/*************************************************************************************************/

ContentsChatItem::WrapColumnFinder::WrapColumnFinder(ChatItem *_item)
  : item(_item),
    layout(0),
    wrapList(item->data(ChatLineModel::WrapListRole).value<ChatLineModel::WrapList>()),
    wordidx(0),
    lineCount(0),
    choppedTrailing(0)
{
}

ContentsChatItem::WrapColumnFinder::~WrapColumnFinder() {
  delete layout;
}

qint16 ContentsChatItem::WrapColumnFinder::nextWrapColumn() {
  if(wordidx >= wrapList.count())
    return -1;

  lineCount++;
  qreal targetWidth = lineCount * item->width() + choppedTrailing;

  qint16 start = wordidx;
  qint16 end = wrapList.count() - 1;

  // check if the whole line fits
  if(wrapList.at(end).endX <= targetWidth) //  || start == end)
    return -1;

  // check if we have a very long word that needs inter word wrap
  if(wrapList.at(start).endX > targetWidth) {
    if(!line.isValid()) {
      layout = item->createLayout(QTextOption::NoWrap);
      layout->beginLayout();
      line = layout->createLine();
      layout->endLayout();
    }
    return line.xToCursor(targetWidth, QTextLine::CursorOnCharacter);
  }

  while(true) {
    if(start + 1 == end) {
      wordidx = end;
      const ChatLineModel::Word &lastWord = wrapList.at(start); // the last word we were able to squeeze in

      // both cases should be cought preliminary
      Q_ASSERT(lastWord.endX <= targetWidth); // ensure that "start" really fits in
      Q_ASSERT(end < wrapList.count()); // ensure that start isn't the last word

      choppedTrailing += lastWord.trailing - (targetWidth - lastWord.endX);
      return wrapList.at(wordidx).start;
    }

    qint16 pivot = (end + start) / 2;
    if(wrapList.at(pivot).endX > targetWidth) {
      end = pivot;
    } else {
      start = pivot;
    }
  }
  Q_ASSERT(false);
  return -1;
}

/*************************************************************************************************/

