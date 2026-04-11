import ctypes
import os
import sys

LIB_PATH = os.path.join(os.path.dirname(__file__), "libboard.so")

try:
    lib = ctypes.CDLL(LIB_PATH)
except OSError as e:
    print(f"Error loading library: {e}")
    print("Please compile first: gcc -shared -fPIC -O2 -o libboard.so Board.c")
    sys.exit(1)


class Board(ctypes.Structure):
    _fields_ = [
        ("squares", ctypes.c_uint8 * 64),
        ("current_color", ctypes.c_uint8),
        ("castling", ctypes.c_bool * 4),
        ("ep_square", ctypes.c_int8),
        ("halfmove_clock", ctypes.c_uint8),
        ("fullmove_number", ctypes.c_uint16),
    ]


lib.board_init.argtypes = [ctypes.POINTER(Board)]
lib.board_init.restype = None

lib.board_move.argtypes = [
    ctypes.POINTER(Board),
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
]
lib.board_move.restype = ctypes.c_int

lib.is_in_check.argtypes = [ctypes.POINTER(Board)]
lib.is_in_check.restype = ctypes.c_bool

lib.has_legal_moves.argtypes = [ctypes.POINTER(Board)]
lib.has_legal_moves.restype = ctypes.c_bool

lib.get_game_state.argtypes = [ctypes.POINTER(Board)]
lib.get_game_state.restype = ctypes.c_int

lib.get_piece_symbol.argtypes = [ctypes.c_uint8]
lib.get_piece_symbol.restype = ctypes.c_char_p


COLOR_WHITE = 0
COLOR_BLACK = 8
PIECE_NONE = 0

MOVE_OK = 0
MOVE_INVALID_FROM = 1
MOVE_INVALID_TO = 2
MOVE_WRONG_COLOR = 3
MOVE_INVALID_PATTERN = 4
MOVE_IN_CHECK = 5
MOVE_CASTLING_THROUGH_CHECK = 6

GAME_ONGOING = 0
GAME_CHECKMATE = 1
GAME_STALEMATE = 2


PIECES = {
    "P": "♙",
    "N": "♘",
    "B": "♗",
    "R": "♖",
    "Q": "♕",
    "K": "♔",
    "p": "♟",
    "n": "♞",
    "b": "♝",
    "r": "♜",
    "q": "♛",
    "k": "♚",
    " ": "·",
}

ERROR_MESSAGES = {
    MOVE_OK: "",
    MOVE_INVALID_FROM: "Нет фигуры в исходной позиции",
    MOVE_INVALID_TO: "Недопустимая целевая позиция",
    MOVE_WRONG_COLOR: "Не ваш ход",
    MOVE_INVALID_PATTERN: "Недопустимый ход для этой фигуры",
    MOVE_IN_CHECK: "Ход оставляет короля под шахом",
    MOVE_CASTLING_THROUGH_CHECK: "Рокировка через битое поле",
}


def parse_move(move_str):
    """Parse move in algebraic notation (e.g., 'e2e4', 'e2-e4')"""
    move_str = move_str.strip().lower().replace("-", "")

    if len(move_str) < 4:
        return None

    try:
        from_file = ord(move_str[0]) - ord("a")
        from_rank = int(move_str[1]) - 1
        to_file = ord(move_str[2]) - ord("a")
        to_rank = int(move_str[3]) - 1

        if not (0 <= from_file <= 7 and 0 <= from_rank <= 7):
            return None
        if not (0 <= to_file <= 7 and 0 <= to_rank <= 7):
            return None

        return (from_file, from_rank, to_file, to_rank)
    except (ValueError, IndexError):
        return None


def print_board(board):
    """Print the chess board with coordinates"""
    print("\n    a   b   c   d   e   f   g   h")
    print("  +---+---+---+---+---+---+---+---+")

    for rank in range(7, -1, -1):
        print(f"{rank + 1} |", end="")
        for file in range(8):
            piece = board.squares[rank * 8 + file]

            symbol_ptr = lib.get_piece_symbol(piece)
            symbol = symbol_ptr.decode("ascii") if symbol_ptr else " "

            display = PIECES.get(symbol, PIECES[" "])

            if (file + rank) % 2 == 0:
                print(f" {display} ", end="|")
            else:
                if symbol.isupper():
                    print(f"\033[47m\033[30m {display} \033[0m", end="|")
                else:
                    print(f"\033[40m\033[97m {display} \033[0m", end="|")
        print(f" {rank + 1}")
        print("  +---+---+---+---+---+---+---+---+")

    print("    a   b   c   d   e   f   g   h\n")


def print_game_info(board, in_check):
    """Print current game information"""
    color = "Белые" if board.current_color == COLOR_WHITE else "Чёрные"
    print(f"Ход: {color}")

    if in_check:
        state = lib.get_game_state(ctypes.byref(board))
        if state == GAME_CHECKMATE:
            winner = "Чёрные" if board.current_color == COLOR_WHITE else "Белые"
            print(f"\n*** МАТ! Победили {winner}! ***\n")
        else:
            print("\n*** ШАХ! ***\n")
    else:
        state = lib.get_game_state(ctypes.byref(board))
        if state == GAME_STALEMATE:
            print("\n*** ПАТ! Ничья! ***\n")

    print(
        f"Ход: {board.fullmove_number} | Полуходов без взятия: {board.halfmove_clock}"
    )

    castling = ""
    if board.castling[0]:
        castling += "K"
    if board.castling[1]:
        castling += "Q"
    if board.castling[2]:
        castling += "k"
    if board.castling[3]:
        castling += "q"
    if castling:
        print(f"Рокировки: {castling}")


def get_move_input():
    """Get move from user"""
    while True:
        try:
            move_str = input("\nВаш ход (например, e2e4 или 'quit'): ").strip()

            if move_str.lower() in ("quit", "exit", "q"):
                return None

            if not move_str:
                continue

            parsed = parse_move(move_str)
            if parsed:
                return parsed

            print("Неверный формат. Используйте формат: e2e4 или e2-e4")
        except EOFError:
            return None


def main():
    """Main game loop"""
    print("=" * 50)
    print("       ТЕРМИНАЛЬНЫЕ ШАХМАТЫ")
    print("=" * 50)
    print("\nФормат хода: e2e4, g1f3, e7e8q (для превращения)")
    print("Команды: quit, exit, q - выход\n")

    board = Board()
    lib.board_init(ctypes.byref(board))

    while True:
        in_check = lib.is_in_check(ctypes.byref(board))
        print_board(board)
        print_game_info(board, in_check)

        move = get_move_input()
        if move is None:
            print("\nИгра окончена.")
            break

        from_file, from_rank, to_file, to_rank = move

        result = lib.board_move(
            ctypes.byref(board), from_file, from_rank, to_file, to_rank
        )

        if result == MOVE_OK:
            pass
        else:
            print(f"\n❌ {ERROR_MESSAGES.get(result, 'Неизвестная ошибка')}")

            state = lib.get_game_state(ctypes.byref(board))
            if state != GAME_ONGOING:
                print_board(board)
                print_game_info(board, False)
                break


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n\nИгра прервана.")
        sys.exit(0)
