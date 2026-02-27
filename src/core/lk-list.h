/*
 * lk-list.h -- internal: the UIK_LIST widget def (docs/table.md).
 */
#ifndef LK_LIST_H
#define LK_LIST_H

#include <lk.h>

#define LK_LIST_DEFAULT_ROW_H 24
#define LK_LIST_BAR_W 6
#define LK_LIST_STEP 30
#define LK_LIST_FLOOR_ROWS 8

lk_widget_def lk_list_widget_def(void);

#endif /* LK_LIST_H */
