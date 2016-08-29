/**
 * \file list-display-terms.h
 * \brief Angband's permanent terms
 *
 * Fields:
 * index - unique index of a term
 * short - short name (abbreviation) of a term
 * full - full name (description) of a term
 * min_c - minimal number of columns
 * min_r - minimal number of rows
 * def_c - default (recommended) number of columns
 * def_r = default (recommended) number of rows
 * max_c - maximal number of columns
 * max_r - maximal number of rows
 * required - this term is not optional
 */

/*      index            short  full             min_c  min_r  def_c  def_r     max_c     max_r   required */
DISPLAY(CAVE,               "",  "Main",             1,     1,    80,    24,  INT_MAX,  INT_MAX,      true)
DISPLAY(MESSAGE_LINE,       "",  "Prompt line",     40,     1,    80,     1,  INT_MAX,        1,      true)

DISPLAY(STATUS_LINE,     "Sts", "Status line",      40,     1,    80,     1,  INT_MAX,        1,     false)
DISPLAY(PLAYER_COMPACT, "P(c)", "Player (compact)", 12,    24,    12,    24,  INT_MAX,  INT_MAX,     false)
DISPLAY(PLAYER_BASIC,   "P(b)", "Player (basic)",   72,    24,    72,    24,  INT_MAX,  INT_MAX,     false)
DISPLAY(PLAYER_EXTRA,   "P(x)", "Player (extra)",   80,    24,    80,    24,  INT_MAX,  INT_MAX,     false)
DISPLAY(INVEN,           "Inv", "Inven/equip",      40,    24,    40,    24,  INT_MAX,  INT_MAX,     false)
DISPLAY(EQUIP,           "Eqp", "Equip/inven",      40,    24,    40,    24,  INT_MAX,  INT_MAX,     false)
DISPLAY(MESSAGES,        "Msg", "Messages",         40,     1,    80,     4,  INT_MAX,  INT_MAX,     false)
DISPLAY(MONSTER,         "Mon", "Monster recall",   12,     3,    24,     8,  INT_MAX,  INT_MAX,     false)
DISPLAY(OBJECT,          "Obj", "Object recall",    12,     3,    24,     8,  INT_MAX,  INT_MAX,     false)
DISPLAY(MONLIST,         "Mls", "Monster list",     12,     3,    24,    12,  INT_MAX,  INT_MAX,     false)
DISPLAY(ITEMLIST,        "Ils", "Item list",        12,     3,    24,    12,  INT_MAX,  INT_MAX,     false)
