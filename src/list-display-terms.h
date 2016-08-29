/**
 * \file list-display-terms.h
 * \brief Angband's permanent terms
 *
 * Fields:
 * index    - unique index of a term
 * name     - full name (description) of a term
 * min_c    - minimal number of columns
 * min_r    - minimal number of rows
 * def_c    - default (recommended) number of columns
 * def_r    - default (recommended) number of rows
 * max_c    - maximal number of columns
 * max_r    - maximal number of rows
 * required - this term is not optional
 */

/*      index            name            min_c  min_r  def_c  def_r     max_c     max_r   required */
DISPLAY(CAVE,           "Main",              1,     1,    80,    24,  INT_MAX,  INT_MAX,      true)
DISPLAY(MESSAGE_LINE,   "Prompt line",      40,     1,    80,     1,  INT_MAX,        1,      true)

DISPLAY(STATUS_LINE,    "Status line",      40,     1,    80,     1,  INT_MAX,        1,     false)
DISPLAY(PLAYER_COMPACT, "Player (compact)", 12,    24,    12,    24,  INT_MAX,  INT_MAX,     false)
DISPLAY(PLAYER_BASIC,   "Player (basic)",   72,    24,    72,    24,  INT_MAX,  INT_MAX,     false)
DISPLAY(PLAYER_EXTRA,   "Player (extra)",   80,    24,    80,    24,  INT_MAX,  INT_MAX,     false)
DISPLAY(INVEN,          "Inven/equip",      40,    24,    40,    24,  INT_MAX,  INT_MAX,     false)
DISPLAY(EQUIP,          "Equip/inven",      40,    24,    40,    24,  INT_MAX,  INT_MAX,     false)
DISPLAY(MESSAGES,       "Messages",         40,     1,    80,     4,  INT_MAX,  INT_MAX,     false)
DISPLAY(MONSTER,        "Monster recall",   12,     3,    24,     8,  INT_MAX,  INT_MAX,     false)
DISPLAY(OBJECT,         "Object recall",    12,     3,    24,     8,  INT_MAX,  INT_MAX,     false)
DISPLAY(MONLIST,        "Monster list",     12,     3,    24,    12,  INT_MAX,  INT_MAX,     false)
DISPLAY(ITEMLIST,       "Item list",        12,     3,    24,    12,  INT_MAX,  INT_MAX,     false)
