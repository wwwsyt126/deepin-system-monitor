#include "list_view.h"
#include "utils.h"
#include <QDebug>
#include <QApplication>
#include <QEvent>
#include <QWheelEvent>
#include <QtMath>

ListView::ListView(int height, QWidget *parent) : QWidget(parent)
{
    installEventFilter(this);  // add event filter
    setMouseTracking(true);   // make MouseMove can response

    rowHeight = height;
    scrollUnit = rowHeight * 9;
    scrollDistance = 0;
    renderOffset = 0;
    titleHeight = 0;

    renderTimer = NULL;
    renderTicker = 0;
    animationFrames = 16;
    animationDuration = 25;

    listItems = new QList<ListItem*>();
    selectionItems = new QList<ListItem*>();
    lastSelectItem = NULL;

    mouseAtScrollArea = false;
    mouseDragScrollbar = false;
    scrollbarDefaultWidth = 3;
    scrollbarDragWidth = 8;

    oldRenderOffset = 0;

    hideScrollbarTimer = NULL;

    renderCounter = 0;

    sortFunctionPtrs = new QList<SortFunctionPtr>();
    sortOrderes = new QList<bool>();
}

void ListView::setSortAlgorithm(QList<SortFunctionPtr> *ptrs, int sortColumn, bool descendingSort) {
    sortFunctionPtrs = ptrs;

    for (int i = 0; i < sortFunctionPtrs->count(); i++) {
        sortOrderes->append(false);
    }
    
    defaultSortColumn = sortColumn;
    defaultSortOrder = descendingSort;
}

void ListView::initItems(QList<ListItem*> items) {
    // Init.
    QList<ListItem*> *newSelectionItems = new QList<ListItem*>();
    ListItem *newLastSelectionItem = NULL;
    
    // Save selection items and last selection item.
    for (ListItem *item:items) {
        for (ListItem *selectionItem:*selectionItems) {
            if (item->hasSameContent(selectionItem)) {
                newSelectionItems->append(item);
                break;
            }
        }
    }
    if (lastSelectItem != NULL) {
        for (ListItem *item:items) {
            if (item->hasSameContent(lastSelectItem)) {
                newLastSelectionItem = item;
                break;
            }
        }
    }
    
    // Update items.
    listItems->clear();
    listItems->append(items);
    
    // Sort once if default sort column hasn't init.
    if (defaultSortColumn != -1) {
        sortItems(defaultSortColumn, defaultSortOrder);
    }
    
    // Restore selection items and last selection item.
    clearSelections();
    addSelections(*newSelectionItems);
    lastSelectItem = newLastSelectionItem;
    
    // Keep scroll position.
    renderOffset = adjustRenderOffset(renderOffset);
    
    // Render.
    repaint();
}

void ListView::addItems(QList<ListItem*> items) {
    listItems->append(items);
    
    if (defaultSortColumn != -1) {
        sortItems(defaultSortColumn, defaultSortOrder);
    }
}

void ListView::clearItems() {
    listItems->clear();
}

void ListView::addSelections(QList<ListItem*> items, bool recordLastSelection) {
    selectionItems->append(items);

    if (recordLastSelection) {
        lastSelectItem = selectionItems->last();
    }
}

void ListView::clearSelections(bool clearLastSelection) {
    selectionItems->clear();

    if (clearLastSelection) {
        lastSelectItem = NULL;
    }
}

void ListView::setTitles(QList<QString> titles, int height) {
    titleNames = titles;
    titleHeight = height;
}

void ListView::setColumnWidths(QList<int> widths) {
    columnWidths = widths;
}

void ListView::mouseMoveEvent(QMouseEvent *mouseEvent) {
    if (mouseDragScrollbar) {
        int barHeight = getScrollbarHeight();

        renderOffset = adjustRenderOffset((mouseEvent->y() - barHeight / 2 - titleHeight) / (rect().height() - titleHeight * 1.0) * listItems->count() * rowHeight);
        repaint();
    } else {
        bool atScrollArea = isMouseAtScrollArea(mouseEvent->x());

        if (atScrollArea != mouseAtScrollArea) {
            mouseAtScrollArea = atScrollArea;
            repaint();
        }
    }
}

void ListView::mousePressEvent(QMouseEvent *mouseEvent) {
    bool atTitleArea = isMouseAtTitleArea(mouseEvent->y());
    bool atScrollArea = isMouseAtScrollArea(mouseEvent->x());

    if (atTitleArea) {
        if (sortFunctionPtrs->count() == titleNames.count() && sortOrderes->count() == titleNames.count()) {
            // Calcuate title widths;
            QList<int> renderWidths = calcuateRenderWidths();

            int columnCounter = 0;
            int columnRenderX = 0;
            for (int renderWidth:renderWidths) {
                if (mouseEvent->x() > columnRenderX && mouseEvent->x() < columnRenderX + renderWidth) {
                    // If switch other column, default order is from top to bottom.
                    if (columnCounter != defaultSortColumn) {
                        (*sortOrderes)[columnCounter] = true;
                    }
                    // If user click same column, just switch reverse order.
                    else {    
                        (*sortOrderes)[columnCounter] = !(*sortOrderes)[columnCounter];
                    }
                    
                    defaultSortColumn = columnCounter;
                    defaultSortOrder = (*sortOrderes)[columnCounter];

                    sortItems(columnCounter, (*sortOrderes)[columnCounter]);
                    
                    repaint();
                    break;
                }

                columnRenderX += renderWidth;
                columnCounter++;
            }
        }
    } else if (atScrollArea) {
        int barHeight = getScrollbarHeight();

        int barY = (renderOffset / listItems->count() * rowHeight * 1.0) * (rect().height() - titleHeight) + titleHeight;
        if (mouseEvent->y() > barY && mouseEvent->y() < barY + barHeight) {
            mouseDragScrollbar = true;
        } else {
            renderOffset = adjustRenderOffset((mouseEvent->y() - barHeight / 2 - titleHeight) / (rect().height() - titleHeight * 1.0) * listItems->count() * rowHeight);
            repaint();
        }

    } else {
        int pressItemIndex = (renderOffset + mouseEvent->y() - titleHeight) / rowHeight;

        if (pressItemIndex < listItems->count()) {
            if (mouseEvent->modifiers() == Qt::ControlModifier) {
                ListItem *item = (*listItems)[pressItemIndex];

                if (selectionItems->contains(item)) {
                    selectionItems->removeOne(item);
                } else {
                    QList<ListItem*> items = QList<ListItem*>();
                    items << item;
                    addSelections(items);
                }
            } else if ((mouseEvent->modifiers() == Qt::ShiftModifier) && !selectionItems->empty()) {
                int lastSelectionIndex = listItems->indexOf(lastSelectItem);
                int selectionStartIndex = std::min(pressItemIndex, lastSelectionIndex);
                int selectionEndIndex = std::max(pressItemIndex, lastSelectionIndex);

                shiftSelect(selectionStartIndex, selectionEndIndex);
            } else {
                clearSelections();

                QList<ListItem*> items = QList<ListItem*>();
                items << (*listItems)[pressItemIndex];
                addSelections(items);
            }

            repaint();
        }
    }
}

void ListView::mouseReleaseEvent(QMouseEvent *) {
    if (mouseDragScrollbar) {
        mouseDragScrollbar = false;
        repaint();
    }
}

void ListView::keyPressEvent(QKeyEvent *keyEvent) {
    if (keyEvent->key() == Qt::Key_Home) {
        if (keyEvent->modifiers() == Qt::ControlModifier) {
            scrollHome();
        } else if (keyEvent->modifiers() == Qt::ShiftModifier) {
            shiftSelectHome();
        }else {
            pressHome();
        }
    } else if (keyEvent->key() == Qt::Key_End) {
        if (keyEvent->modifiers() == Qt::ControlModifier) {
            scrollEnd();
        } else if (keyEvent->modifiers() == Qt::ShiftModifier) {
            shiftSelectEnd();
        } else {
            pressEnd();
        }
    } else if (keyEvent->key() == Qt::Key_Up) {
        if (keyEvent->modifiers() == Qt::ShiftModifier) {
            shiftSelectUp();
        } else {
            pressUp();
        }
    } else if (keyEvent->key() == Qt::Key_Down) {
        if (keyEvent->modifiers() == Qt::ShiftModifier) {
            shiftSelectDown();
        } else {
            pressDown();
        }
    } else if (keyEvent->key() == Qt::Key_PageUp) {
        if (keyEvent->modifiers() == Qt::ControlModifier) {
            scrollPageUp();
        } else if (keyEvent->modifiers() == Qt::ShiftModifier) {
            shiftSelectPageUp();
        } else {
            pressPageUp();
        }
    } else if (keyEvent->key() == Qt::Key_PageDown) {
        if (keyEvent->modifiers() == Qt::ControlModifier) {
            scrollPageDown();
        } else if (keyEvent->modifiers() == Qt::ShiftModifier) {
            shiftSelectPageDown();
        } else {
            pressPageDown();
        }
    } else if (keyEvent->key() == Qt::Key_A) {
        if (keyEvent->modifiers() == Qt::ControlModifier) {
            selectAll();
        }
    }
}

bool ListView::eventFilter(QObject *, QEvent *)
{
    return false;
}

bool ListView::isMouseAtScrollArea(int x) {
    return (x > rect().x() + rect().width() - scrollbarDragWidth) && (x < rect().x() + rect().width());
}

bool ListView::isMouseAtTitleArea(int y) {
    return (y > rect().y() && y < rect().y() + titleHeight);
}

void ListView::selectAll() {
    oldRenderOffset = renderOffset;

    clearSelections();

    addSelections(*listItems);

    renderOffset = 0;

    repaint();
}

void ListView::pressHome() {
    oldRenderOffset = renderOffset;

    clearSelections();

    QList<ListItem*> items = QList<ListItem*>();
    items << listItems->first();
    addSelections(items);

    renderOffset = 0;

    repaint();
}

void ListView::pressEnd() {
    oldRenderOffset = renderOffset;

    clearSelections();

    QList<ListItem*> items = QList<ListItem*>();
    items << listItems->last();
    addSelections(items);

    renderOffset = listItems->count() * rowHeight - rect().height() + titleHeight;

    repaint();
}

void ListView::selectScrollUp(int scrollOffset) {
    oldRenderOffset = renderOffset;

    if (selectionItems->empty()) {
        pressHome();
    } else {
        int firstIndex = listItems->count();
        for (ListItem *item:*selectionItems) {
            int index = listItems->indexOf(item);
            if (index < firstIndex) {
                firstIndex = index;
            }
        }

        if (firstIndex != -1) {
            firstIndex = std::max(0, firstIndex - scrollOffset);

            clearSelections();

            QList<ListItem*> items = QList<ListItem*>();
            items << (*listItems)[firstIndex];

            addSelections(items);

            int itemIndex = firstIndex - 1;
            int itemOffset = adjustRenderOffset(itemIndex * rowHeight + titleHeight);
            if ((renderOffset / rowHeight) > itemIndex) {
                renderOffset = itemOffset;
            }

            repaint();
        }
    }
}

void ListView::selectScrollDown(int scrollOffset) {
    oldRenderOffset = renderOffset;

    if (selectionItems->empty()) {
        pressHome();
    } else {
        int lastIndex = 0;
        for (ListItem *item:*selectionItems) {
            int index = listItems->indexOf(item);
            if (index > lastIndex) {
                lastIndex = index;
            }
        }

        if (lastIndex != -1) {
            lastIndex = std::min(listItems->count() - 1, lastIndex + scrollOffset);

            clearSelections(false);

            QList<ListItem*> items = QList<ListItem*>();
            items << (*listItems)[lastIndex];

            addSelections(items);

            int itemIndex = lastIndex + 1;
            int itemOffset = adjustRenderOffset(itemIndex * rowHeight - rect().height() + titleHeight);
            if (((renderOffset + rect().height() - titleHeight) / rowHeight) < itemIndex) {
                renderOffset = itemOffset;
            }

            repaint();
        }
    }
}

void ListView::pressUp() {
    selectScrollUp(1);
}

void ListView::pressDown() {
    selectScrollDown(1);
}

void ListView::pressPageUp() {
    selectScrollUp((rect().height() - titleHeight) / rowHeight);
}

void ListView::pressPageDown() {
    selectScrollDown((rect().height() - titleHeight) / rowHeight);
}

void ListView::scrollPageUp() {
    renderOffset = adjustRenderOffset(renderOffset - (rect().height() - titleHeight));

    repaint();
}

void ListView::scrollPageDown() {
    renderOffset = adjustRenderOffset(renderOffset + (rect().height() - titleHeight));

    repaint();
}

void ListView::scrollHome() {
    renderOffset = 0;

    repaint();
}

void ListView::scrollEnd() {
    renderOffset = listItems->count() * rowHeight - rect().height() + titleHeight;

    repaint();
}

void ListView::shiftSelect(int selectionStartIndex, int selectionEndIndex) {
    // Note: Shift operation always selection bound from last selection index to current index.
    // So we don't need *clear* lastSelectionIndex for keep shift + button is right logic.
    clearSelections(false);
    QList<ListItem*> items = QList<ListItem*>();
    int index = 0;
    for (ListItem *item:*listItems) {
        if (index >= selectionStartIndex && index <= selectionEndIndex) {
            items << item;
        }

        index++;
    }
    // Note: Shift operation always selection bound from last selection index to current index.
    // So we don't need *record* lastSelectionIndex for keep shift + button is right logic.
    addSelections(items, false);
}

void ListView::shiftSelectHome() {
    if (selectionItems->empty()) {
        pressHome();
    } else {
        int lastSelectionIndex = listItems->indexOf(lastSelectItem);
        shiftSelect(0, lastSelectionIndex);

        renderOffset = 0;

        repaint();
    }
}

void ListView::shiftSelectEnd() {
    if (selectionItems->empty()) {
        pressEnd();
    } else {
        int lastSelectionIndex = listItems->indexOf(lastSelectItem);
        shiftSelect(lastSelectionIndex, listItems->count() - 1);

        renderOffset = listItems->count() * rowHeight - rect().height() + titleHeight;

        repaint();
    }
}

void ListView::shiftSelectScrollUp(int scrollOffset) {
    oldRenderOffset = renderOffset;

    if (selectionItems->empty()) {
        pressHome();
    } else {
        int firstIndex = listItems->count();
        int lastIndex = 0;
        for (ListItem *item:*selectionItems) {
            int index = listItems->indexOf(item);

            if (index < firstIndex) {
                firstIndex = index;
            }

            if (index > lastIndex) {
                lastIndex = index;
            }
        }

        if (firstIndex != -1) {
            int lastSelectionIndex = listItems->indexOf(lastSelectItem);
            int selectionStartIndex, selectionEndIndex;

            if (lastIndex == lastSelectionIndex) {
                selectionStartIndex = std::max(0, firstIndex - scrollOffset);
                selectionEndIndex = lastSelectionIndex;
            } else {
                selectionStartIndex = firstIndex;
                selectionEndIndex = std::max(0, lastIndex - scrollOffset);
            }

            shiftSelect(selectionStartIndex, selectionEndIndex);

            renderOffset = adjustRenderOffset((selectionStartIndex - 1) * rowHeight + titleHeight);

            repaint();
        }
    }
}

void ListView::shiftSelectScrollDown(int scrollOffset) {
    oldRenderOffset = renderOffset;

    if (selectionItems->empty()) {
        pressHome();
    } else {
        int firstIndex = listItems->count();
        int lastIndex = 0;
        for (ListItem *item:*selectionItems) {
            int index = listItems->indexOf(item);

            if (index < firstIndex) {
                firstIndex = index;
            }

            if (index > lastIndex) {
                lastIndex = index;
            }
        }

        if (firstIndex != -1) {
            int lastSelectionIndex = listItems->indexOf(lastSelectItem);
            int selectionStartIndex, selectionEndIndex;

            if (firstIndex == lastSelectionIndex) {
                selectionStartIndex = firstIndex;
                selectionEndIndex = std::min(listItems->count() - 1, lastIndex + scrollOffset);
            } else {
                selectionStartIndex = std::min(listItems->count() - 1, firstIndex + scrollOffset);
                selectionEndIndex = lastIndex;
            }

            shiftSelect(selectionStartIndex, selectionEndIndex);

            renderOffset = adjustRenderOffset((selectionEndIndex + 1) * rowHeight + titleHeight - rect().height());

            repaint();
        }
    }
}

void ListView::shiftSelectUp() {
    shiftSelectScrollUp(1);
}

void ListView::shiftSelectDown() {
    shiftSelectScrollDown(1);
}

void ListView::shiftSelectPageUp() {
    shiftSelectScrollUp((rect().height() - titleHeight) / rowHeight);
}

void ListView::shiftSelectPageDown() {
    shiftSelectScrollDown((rect().height() - titleHeight) / rowHeight);
}

int ListView::adjustRenderOffset(int offset) {
    return std::max(0, std::min(offset, listItems->count() * rowHeight - rect().height() + titleHeight));
}

void ListView::wheelEvent(QWheelEvent *event)
{
    if (event->orientation() == Qt::Vertical) {
        oldRenderOffset = renderOffset;

        int scrollStep = event->angleDelta().y() / 120;

        int newRenderOffset = renderOffset - scrollStep * scrollUnit;
        newRenderOffset = adjustRenderOffset(newRenderOffset);

        if (newRenderOffset != renderOffset) {
            if (renderTimer == NULL || !renderTimer->isActive()) {
                // If timer is inactive, start scroll timer.
                scrollStartY = renderOffset;
                scrollDistance = newRenderOffset - renderOffset;

                startScroll();
            } else {
                // If timer is active, just add scroll offset make scroll faster and *smooth*.
                scrollDistance -= scrollStep * rowHeight;
            }
        }
    }

    event->accept();
}

void ListView::startScroll() {
    if (renderTimer == NULL || !renderTimer->isActive()) {
        renderTicker = 0;
        renderTimer = new QTimer();
        connect(renderTimer, SIGNAL(timeout()), this, SLOT(renderAnimation()));
        renderTimer->start(animationDuration);
    }
}

void ListView::renderAnimation() {
    if (renderTicker <= animationFrames) {

        renderOffset = adjustRenderOffset(scrollStartY + Utils::easeInOut(renderTicker / (animationFrames * 1.0)) * scrollDistance);

        repaint();

        renderTicker++;
    } else {
        renderTimer->stop();
    }
}

void ListView::paintEvent(QPaintEvent *) {
    // qDebug() << "************** " << renderCounter;
    renderCounter++;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Calcuate title widths;
    QList<int> renderWidths = calcuateRenderWidths();

    // Draw title.
    int renderY = 0;
    int renderHeight = 0;
    if (titleHeight > 0) {
        int columnCounter = 0;
        int columnRenderX = 0;
        for (int renderWidth:renderWidths) {
            painter.setOpacity(1);
            Utils::setFontSize(painter, 11);
            painter.setPen(QPen(QColor("#666666")));
            painter.drawText(QRect(columnRenderX, 0, renderWidth, titleHeight), Qt::AlignCenter, titleNames[columnCounter]);

            columnRenderX += renderWidth;
            columnCounter++;
        }

        renderY += titleHeight;
        renderHeight += titleHeight;
    }

    // Draw context.
    painter.setClipRect(QRectF(rect().x(), rect().y() + rowHeight, rect().width(), rect().height() - rowHeight));

    int rowCounter = 0;
    for (ListItem *item:*listItems) {
        if (rowCounter > ((renderOffset - rowHeight) / rowHeight)) {
            bool isSelect = selectionItems->contains(item);
            item->renderBackground(QRect(0, renderY + rowCounter * rowHeight - renderOffset, rect().width(), rowHeight), &painter, rowCounter, isSelect);

            int columnCounter = 0;
            int columnRenderX = 0;
            for (int renderWidth:renderWidths) {
                item->render(QRect(columnRenderX, renderY + rowCounter * rowHeight - renderOffset, renderWidth, rowHeight), &painter, columnCounter, isSelect);

                columnRenderX += renderWidth;
                columnCounter++;
            }

            renderHeight += rowHeight;

            if (renderHeight > rect().height()) {
                break;
            }
        }

        rowCounter++;
    }

    painter.setClipRect(QRectF(rect()));

    // Draw scrollbar.
    if (mouseAtScrollArea) {
        paintScrollbar(&painter);
    } else if (oldRenderOffset != renderOffset) {
        paintScrollbar(&painter);

        startHideScrollbar();
    }
}

void ListView::startHideScrollbar() {
    if (hideScrollbarTimer != NULL) {
        hideScrollbarTimer->stop();
    }

    hideScrollbarTimer = new QTimer();
    connect(hideScrollbarTimer, SIGNAL(timeout()), this, SLOT(hideScrollbar()));
    hideScrollbarTimer->start(1000);
}

void ListView::hideScrollbar() {
    oldRenderOffset = renderOffset;

    repaint();
}

void ListView::paintScrollbar(QPainter *painter) {
    if (listItems->count() * rowHeight > rect().height() - titleHeight) {
        qreal barOpacitry = 0;
        if (mouseDragScrollbar) {
            barOpacitry = 0.8;
        } else {
            if (mouseAtScrollArea) {
                barOpacitry = 0.5;
            } else {
                barOpacitry = 0.1;
            }
        }

        int barWidth = mouseAtScrollArea ? scrollbarDragWidth : scrollbarDefaultWidth;
        int barRadius = 5;

        int barY = static_cast<int>((renderOffset / (listItems->count() * rowHeight * 1.0)) * (rect().height() - titleHeight) + titleHeight);
        int barHeight = getScrollbarHeight();

        painter->setOpacity(barOpacitry);
        QPainterPath path;
        path.addRoundedRect(QRectF(rect().x() + rect().width() - barWidth, barY + barRadius, barWidth, barHeight - barRadius * 2), barRadius, barRadius);
        painter->fillPath(path, QColor("#F5F5F5"));
    }
}

int ListView::getScrollbarHeight() {
    return std::max(static_cast<int>((rect().height() - titleHeight) / (listItems->count() * rowHeight * 1.0) * rect().height()), 30);
}

QList<int> ListView::calcuateRenderWidths() {
    QList<int> renderWidths;
    if (columnWidths.contains(-1)) {
        for (int columnWidth:columnWidths) {
            if (columnWidth != -1) {
                renderWidths << columnWidth;
            } else {
                int totalWidthOfOtherColumns = 0;
                for (auto w:columnWidths) {
                    if (w != -1) {
                        totalWidthOfOtherColumns += w;
                    }
                }

                renderWidths << rect().width() - totalWidthOfOtherColumns;
            }
        }
    } else {
        renderWidths = columnWidths;
    }

    return renderWidths;
}

void ListView::sortItems(int column, bool descendingSort) {
    qSort(listItems->begin(), listItems->end(), [&](const ListItem *item1, const ListItem *item2) {
            return (*sortFunctionPtrs)[column](item1, item2, descendingSort);
        });
}
