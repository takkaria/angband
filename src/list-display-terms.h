/**
 * \file list-display-terms.h
 * \brief Angband's permanent terms
 *
 * Fields:
 * index - unique index of a term
 * desc - description of a term
 * min_cols - minimal number of columns
 * min_rows - minimal number of rows
 * max_cols - maximal number of columns
 * max_rows - maximal number of rows
 * required - this term is not optional
 */

/*      index           desc              min_cols  min_rows    max_cols     max_rows   required */
DISPLAY(CAVE,           "Main map",              1,        1,    INT_MAX,     INT_MAX,      true)
DISPLAY(MESSAGE_LINE,   "Messages line",        40,        1,    INT_MAX,           1,      true)
DISPLAY(SIDEBAR,        "Sidebar",              13,       24,         13,          24,      true)
DISPLAY(STATUS_LINE,    "Status line",          40,        1,    INT_MAX,           1,      true)

DISPLAY(INVEN,          "Inven/equip",          40,       24,    INT_MAX,     INT_MAX,     false)
DISPLAY(EQUIP,          "Equip/inven",          40,       24,    INT_MAX,     INT_MAX,     false)
DISPLAY(PLAYER_BASIC,   "Player (basic)",       72,       24,    INT_MAX,     INT_MAX,     false)
DISPLAY(PLAYER_EXTRA,   "Player (extra)",       80,       24,    INT_MAX,     INT_MAX,     false)
DISPLAY(MESSAGES,       "Messages",              1,        1,    INT_MAX,     INT_MAX,     false)
DISPLAY(MONSTER,        "Monster recall",        1,        1,    INT_MAX,     INT_MAX,     false)
DISPLAY(OBJECT,         "Object recall",         1,        1,    INT_MAX,     INT_MAX,     false)
DISPLAY(MONLIST,        "Monster list",          1,        1,    INT_MAX,     INT_MAX,     false)
DISPLAY(ITEMLIST,       "Item list",             1,        1,    INT_MAX,     INT_MAX,     false)
