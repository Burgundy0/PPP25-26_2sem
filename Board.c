#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define PIECE_NONE   0
#define PIECE_PAWN   1
#define PIECE_KNIGHT 2
#define PIECE_BISHOP 3
#define PIECE_ROOK   4
#define PIECE_QUEEN  5
#define PIECE_KING   6

#define COLOR_WHITE 0
#define COLOR_BLACK 8

#define MAKE_PIECE(type, color) ((type) | (color))
#define GET_TYPE(piece) ((piece) & 0x07)
#define GET_COLOR(piece) ((piece) & 0x08)

typedef struct {
    uint8_t squares[64];
    uint8_t current_color;  // 0 = белые, 8 = чёрные
    bool castling[4];       // KQkq - рокировки для белых/чёрных
    int8_t ep_square;       // Цель для взятия на проходе (-1 если нет)
    uint8_t halfmove_clock; // Для правила 50 ходов
    uint16_t fullmove_number;
} Board;

// Инициализация доски начальной позицией
void board_init(Board *board) {
    memset(board->squares, 0, sizeof(board->squares));

    // Расстановка пешек
    for (int i = 0; i < 8; i++) {
        board->squares[i + 8] = MAKE_PIECE(PIECE_PAWN, COLOR_WHITE);
        board->squares[i + 48] = MAKE_PIECE(PIECE_PAWN, COLOR_BLACK);
    }

    // Расстановка фигур
    const uint8_t back_rank[] = {
        PIECE_ROOK, PIECE_KNIGHT, PIECE_BISHOP, PIECE_QUEEN,
        PIECE_KING, PIECE_BISHOP, PIECE_KNIGHT, PIECE_ROOK
    };

    for (int i = 0; i < 8; i++) {
        board->squares[i] = MAKE_PIECE(back_rank[i], COLOR_WHITE);
        board->squares[i + 56] = MAKE_PIECE(back_rank[i], COLOR_BLACK);
    }

    board->current_color = COLOR_WHITE;
    board->castling[0] = board->castling[1] = true;  // Белые KQ
    board->castling[2] = board->castling[3] = true;  // Чёрные kq
    board->ep_square = -1;
    board->halfmove_clock = 0;
    board->fullmove_number = 1;
}

// Получить фигуру на (file, rank) - 0-индексировано
static inline uint8_t board_get(const Board *board, int file, int rank) {
    if (file < 0 || file > 7 || rank < 0 || rank > 7) return PIECE_NONE;
    return board->squares[rank * 8 + file];
}

// Установить фигуру на (file, rank)
static inline void board_set(Board *board, int file, int rank, uint8_t piece) {
    board->squares[rank * 8 + file] = piece;
}

// Проверка, атакована ли клетка данным цветом
bool is_attacked(const Board *board, int file, int rank, uint8_t color) {
    // Атаки пешек
    int pawn_dir = (color == COLOR_WHITE) ? -1 : 1;
    int pawn_rank = rank - pawn_dir;
    if (pawn_rank >= 0 && pawn_rank <= 7) {
        if (file > 0) {
            uint8_t p = board_get(board, file - 1, pawn_rank);
            if (GET_TYPE(p) == PIECE_PAWN && GET_COLOR(p) == color) return true;
        }
        if (file < 7) {
            uint8_t p = board_get(board, file + 1, pawn_rank);
            if (GET_TYPE(p) == PIECE_PAWN && GET_COLOR(p) == color) return true;
        }
    }

    // Атаки коня
    static const int knight_moves[8][2] = {
        {-2, -1}, {-2, 1}, {-1, -2}, {-1, 2},
        {1, -2}, {1, 2}, {2, -1}, {2, 1}
    };
    for (int i = 0; i < 8; i++) {
        int nf = file + knight_moves[i][0];
        int nr = rank + knight_moves[i][1];
        uint8_t p = board_get(board, nf, nr);
        if (GET_TYPE(p) == PIECE_KNIGHT && GET_COLOR(p) == color) return true;
    }

    // Атаки короля
    for (int df = -1; df <= 1; df++) {
        for (int dr = -1; dr <= 1; dr++) {
            if (df == 0 && dr == 0) continue;
            uint8_t p = board_get(board, file + df, rank + dr);
            if (GET_TYPE(p) == PIECE_KING && GET_COLOR(p) == color) return true;
        }
    }

    // Скользящие фигуры (ладья/ферзь по вертикали/горизонтали)
    static const int dirs[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (int d = 0; d < 4; d++) {
        for (int dist = 1; dist < 8; dist++) {
            int sf = file + dirs[d][0] * dist;
            int sr = rank + dirs[d][1] * dist;
            uint8_t p = board_get(board, sf, sr);
            if (p == PIECE_NONE) continue;
            if (GET_COLOR(p) == color) {
                int t = GET_TYPE(p);
                if (t == PIECE_ROOK || t == PIECE_QUEEN) return true;
            }
            break;
        }
    }

    // Скользящие фигуры по диагонали (слон/ферзь)
    static const int diag_dirs[4][2] = {{-1, -1}, {-1, 1}, {1, -1}, {1, 1}};
    for (int d = 0; d < 4; d++) {
        for (int dist = 1; dist < 8; dist++) {
            int sf = file + diag_dirs[d][0] * dist;
            int sr = rank + diag_dirs[d][1] * dist;
            uint8_t p = board_get(board, sf, sr);
            if (p == PIECE_NONE) continue;
            if (GET_COLOR(p) == color) {
                int t = GET_TYPE(p);
                if (t == PIECE_BISHOP || t == PIECE_QUEEN) return true;
            }
            break;
        }
    }

    return false;
}

// Найти позицию короля
static int find_king(const Board *board, uint8_t color) {
    for (int i = 0; i < 64; i++) {
        uint8_t p = board->squares[i];
        if (GET_TYPE(p) == PIECE_KING && GET_COLOR(p) == color) return i;
    }
    return -1;
}

// Проверка, под шахом ли король текущего игрока
bool is_in_check(const Board *board) {
    int king_pos = find_king(board, board->current_color);
    if (king_pos < 0) return false;
    return is_attacked(board, king_pos % 8, king_pos / 8,
                       board->current_color ^ COLOR_BLACK);
}

// Коды ошибок хода
#define MOVE_OK 0
#define MOVE_INVALID_FROM 1
#define MOVE_INVALID_TO 2
#define MOVE_WRONG_COLOR 3
#define MOVE_INVALID_PATTERN 4
#define MOVE_IN_CHECK 5
#define MOVE_CASTLING_THROUGH_CHECK 6

int board_move(Board *board, int from_file, int from_rank, int to_file, int to_rank) {
    uint8_t piece = board_get(board, from_file, from_rank);

    if (piece == PIECE_NONE) return MOVE_INVALID_FROM;
    if (GET_COLOR(piece) != board->current_color) return MOVE_WRONG_COLOR;
    if (from_file == to_file && from_rank == to_rank) return MOVE_INVALID_TO;

    uint8_t target = board_get(board, to_file, to_rank);
    if (target != PIECE_NONE && GET_COLOR(target) == board->current_color) return MOVE_INVALID_TO;

    int piece_type = GET_TYPE(piece);
    int df = to_file - from_file;
    int dr = to_rank - from_rank;
    int abs_df = df < 0 ? -df : df;
    int abs_dr = dr < 0 ? -dr : dr;

    bool is_capture = (target != PIECE_NONE) ||
                      (piece_type == PIECE_PAWN && df != 0 && target == PIECE_NONE);

    // Проверка паттерна хода для каждого типа фигур
    switch (piece_type) {
        case PIECE_PAWN: {
            int dir = (GET_COLOR(piece) == COLOR_WHITE) ? 1 : -1;
            int start_rank = (GET_COLOR(piece) == COLOR_WHITE) ? 1 : 6;

            if (df == 0) {
                // Движение вперёд
                if (dr == dir && target == PIECE_NONE) break;
                if (from_rank == start_rank && dr == 2 * dir &&
                    target == PIECE_NONE && board_get(board, from_file, from_rank + dir) == PIECE_NONE) break;
            } else if (abs_df == 1 && dr == dir) {
                // Взятие
                if (target != PIECE_NONE || (board->ep_square == to_rank * 8 + to_file)) break;
            }
            return MOVE_INVALID_PATTERN;
        }

        case PIECE_KNIGHT:
            if (!((abs_df == 2 && abs_dr == 1) || (abs_df == 1 && abs_dr == 2)))
                return MOVE_INVALID_PATTERN;
            break;

        case PIECE_BISHOP:
            if (abs_df != abs_dr || abs_df == 0) return MOVE_INVALID_PATTERN;
            // Проверка, что путь свободен
            for (int i = 1; i < abs_df; i++) {
                if (board_get(board, from_file + (df > 0 ? i : -i),
                              from_rank + (dr > 0 ? i : -i)) != PIECE_NONE)
                    return MOVE_INVALID_PATTERN;
            }
            break;

        case PIECE_ROOK:
            if (df != 0 && dr != 0) return MOVE_INVALID_PATTERN;
            int dist = (abs_df > abs_dr) ? abs_df : abs_dr;
            for (int i = 1; i < dist; i++) {
                int ifile = from_file + (df != 0 ? (df > 0 ? i : -i) : 0);
                int irank = from_rank + (dr != 0 ? (dr > 0 ? i : -i) : 0);
                if (board_get(board, ifile, irank) != PIECE_NONE)
                    return MOVE_INVALID_PATTERN;
            }
            break;

        case PIECE_QUEEN:
            if (df != 0 && dr != 0 && abs_df != abs_dr) return MOVE_INVALID_PATTERN;
            dist = (abs_df > abs_dr) ? abs_df : abs_dr;
            for (int i = 1; i < dist; i++) {
                int ifile = from_file + (df != 0 ? (df > 0 ? i : -i) : 0);
                int irank = from_rank + (dr != 0 ? (dr > 0 ? i : -i) : 0);
                if (board_get(board, ifile, irank) != PIECE_NONE)
                    return MOVE_INVALID_PATTERN;
            }
            break;

        case PIECE_KING:
            if (abs_df > 1 || abs_dr > 1) {
                // Рокировка
                if (abs_df != 2 || dr != 0) return MOVE_INVALID_PATTERN;
                int king_rank = (GET_COLOR(piece) == COLOR_WHITE) ? 0 : 7;
                if (from_rank != king_rank) return MOVE_INVALID_PATTERN;

                int idx = (df > 0) ? 1 : 0;
                if (GET_COLOR(piece) == COLOR_BLACK) idx += 2;
                if (!board->castling[idx]) return MOVE_INVALID_PATTERN;

                // Проверка, что путь свободен и не под ударом
                int step = (df > 0) ? 1 : -1;
                for (int i = 1; i <= 2; i++) {
                    int cf = from_file + step * i;
                    if (is_attacked(board, cf, from_rank, board->current_color ^ COLOR_BLACK))
                        return MOVE_CASTLING_THROUGH_CHECK;
                }
                break;
            }
            break;
    }

    // Сделать ход
    board->squares[from_rank * 8 + from_file] = PIECE_NONE;

    // Обработка специальных ходов
    if (piece_type == PIECE_KING && abs_df == 2) {
        // Рокировка
        int rook_from = (df > 0) ? 7 : 0;
        int rook_to = (df > 0) ? 5 : 3;
        board->squares[from_rank * 8 + rook_to] = board->squares[from_rank * 8 + rook_from];
        board->squares[from_rank * 8 + rook_from] = PIECE_NONE;

        // Отключить рокировку для этого цвета
        int cidx = (GET_COLOR(piece) == COLOR_WHITE) ? 0 : 2;
        board->castling[cidx] = board->castling[cidx + 1] = false;
    } else if (piece_type == PIECE_PAWN) {
        // Взятие на проходе
        if (df != 0 && target == PIECE_NONE) {
            int cap_rank = from_rank;
            board->squares[cap_rank * 8 + to_file] = PIECE_NONE;
        }

        // Установка цели для взятия на проходе
        if (abs_dr == 2) {
            board->ep_square = (from_rank + (dr > 0 ? 1 : -1)) * 8 + from_file;
        } else {
            board->ep_square = -1;
        }

        // Превращение пешки (автоматически в ферзя)
        if (to_rank == 0 || to_rank == 7) {
            piece = MAKE_PIECE(PIECE_QUEEN, GET_COLOR(piece));
        }
    } else {
        board->ep_square = -1;
    }

    // Обновление прав на рокировку
    if (piece_type == PIECE_KING) {
        int cidx = (GET_COLOR(piece) == COLOR_WHITE) ? 0 : 2;
        board->castling[cidx] = board->castling[cidx + 1] = false;
    } else if (piece_type == PIECE_ROOK) {
        if (from_file == 0 && from_rank == 0) board->castling[0] = false;
        if (from_file == 7 && from_rank == 0) board->castling[1] = false;
        if (from_file == 0 && from_rank == 7) board->castling[2] = false;
        if (from_file == 7 && from_rank == 7) board->castling[3] = false;
    }

    board->squares[to_rank * 8 + to_file] = piece;

    // Обновление счётчиков
    if (piece_type == PIECE_PAWN || is_capture) {
        board->halfmove_clock = 0;
    } else {
        board->halfmove_clock++;
    }

    // Временно поменять цвет для проверки шаха
    uint8_t old_color = board->current_color;
    board->current_color ^= COLOR_BLACK;
    bool in_check = is_in_check(board);
    board->current_color = old_color;  // Вернуть назад

    if (in_check) {
        // Отменить ход - вернуть фигуру на источник
        board->squares[from_rank * 8 + from_file] = piece;
        // Отменить специальные ходы
        if (piece_type == PIECE_KING && abs_df == 2) {
            // Отменить рокировку
            int rook_from = (df > 0) ? 7 : 0;
            int rook_to = (df > 0) ? 5 : 3;
            board->squares[from_rank * 8 + rook_from] = board->squares[from_rank * 8 + rook_to];
            board->squares[from_rank * 8 + rook_to] = PIECE_NONE;
        } else if (piece_type == PIECE_PAWN && df != 0 && target == PIECE_NONE) {
            // Отменить взятие на проходе
            int cap_rank = from_rank;
            board->squares[cap_rank * 8 + to_file] = MAKE_PIECE(PIECE_PAWN, old_color ^ COLOR_BLACK);
        }
        board->squares[to_rank * 8 + to_file] = target;
        return MOVE_IN_CHECK;
    }

    // Поменять цвет по-настоящему
    if (board->current_color == COLOR_BLACK) {
        board->fullmove_number++;
    }
    board->current_color ^= COLOR_BLACK;

    return MOVE_OK;
}

// Проверка, есть ли у текущего игрока легальные ходы
bool has_legal_moves(const Board *board) {
    for (int from = 0; from < 64; from++) {
        uint8_t p = board->squares[from];
        if (GET_COLOR(p) != board->current_color) continue;

        for (int to = 0; to < 64; to++) {
            Board temp = *board;
            if (board_move(&temp, from % 8, from / 8, to % 8, to / 8) == MOVE_OK) {
                return true;
            }
        }
    }
    return false;
}

// Получить состояние игры: 0=идёт, 1=мат, 2=пат
int get_game_state(const Board *board) {
    if (!has_legal_moves((Board*)board)) {
        return is_in_check(board) ? 1 : 2;
    }
    return 0;
}

// Преобразовать клетку в алгебраическую нотацию
void square_to_algebraic(int file, int rank, char *out) {
    out[0] = 'a' + file;
    out[1] = '1' + rank;
    out[2] = '\0';
}

// Получить символ фигуры для отображения
const char* get_piece_symbol(uint8_t piece) {
    if (piece == PIECE_NONE) return " ";

    static const char* white_pieces[] = {" ", "P", "N", "B", "R", "Q", "K"};
    static const char* black_pieces[] = {" ", "p", "n", "b", "r", "q", "k"};

    int type = GET_TYPE(piece);
    int color = GET_COLOR(piece);

    return (color == COLOR_WHITE) ? white_pieces[type] : black_pieces[type];
}
