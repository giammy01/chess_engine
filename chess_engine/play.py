#!/usr/bin/env python3
"""
play.py — play against the Nitro engine.

Drives the C++ engine over UCI, exactly the way a real chess GUI does, so the
engine binary needs no special support. python-chess enforces the rules;
pygame draws the board.

  python3 play.py                              # you are White, 1s/move
  python3 play.py --color black --movetime 3
  python3 play.py --engine ./build/nitro --hash 256

Controls
  click a piece then its destination (dragging works too)
  left / right   take back / replay      f  flip board
  n              new game                q / Esc  quit
"""

from __future__ import annotations

import argparse
import os
import queue
import sys
import threading

import chess
import chess.engine
import pygame

# ---------------------------------------------------------------- palette
INK        = (25, 22, 20)
PANEL      = (36, 31, 26)
PANEL_2    = (44, 38, 32)
LINE       = (58, 50, 42)
CREAM      = (239, 225, 198)
CREAM_DIM  = (184, 171, 146)
WALNUT     = (139, 94, 60)
BRASS      = (201, 162, 39)
WHITE_BAR  = (236, 231, 220)
DARK_BAR   = (62, 55, 46)
GOOD       = (129, 182, 76)

SQ = 78                       # square size
BOARD = SQ * 8
MARGIN = 22
BAR_W = 34
PANEL_W = 300
WIDTH = MARGIN + BOARD + 12 + BAR_W + 14 + PANEL_W + MARGIN
HEIGHT = MARGIN + BOARD + MARGIN + 42

GLYPH = {chess.KING: "\u265A", chess.QUEEN: "\u265B", chess.ROOK: "\u265C",
         chess.BISHOP: "\u265D", chess.KNIGHT: "\u265E", chess.PAWN: "\u265F"}


class EngineWorker:
    """
    Runs engine searches on a background thread.

    Without this the window freezes while the engine thinks, which at a
    three-second move time is indistinguishable from a crash.
    """

    def __init__(self, path: str, hash_mb: int, movetime: float):
        if not os.path.exists(path):
            sys.exit(f"Engine not found: {path}\n"
                     f"Build it first:  cd build && cmake .. && make")
        self.engine = chess.engine.SimpleEngine.popen_uci(path)
        try:
            self.engine.configure({"Hash": hash_mb})
        except Exception:
            pass  # engine may not expose a Hash option
        self.movetime = movetime
        self.out: queue.Queue = queue.Queue()
        self.busy = False
        self.name = self.engine.id.get("name", "engine")

    def request(self, board: chess.Board):
        self.busy = True
        snapshot = board.copy()

        def run():
            try:
                res = self.engine.play(
                    snapshot, chess.engine.Limit(time=self.movetime),
                    info=chess.engine.INFO_ALL)
                self.out.put(("move", res.move, res.info))
            except Exception as exc:
                self.out.put(("error", None, str(exc)))

        threading.Thread(target=run, daemon=True).start()

    def poll(self):
        try:
            item = self.out.get_nowait()
            self.busy = False
            return item
        except queue.Empty:
            return None

    def close(self):
        try:
            self.engine.quit()
        except Exception:
            pass


class Game:
    def __init__(self, args):
        pygame.init()
        pygame.display.set_caption("Nitro")
        self.screen = pygame.display.set_mode((WIDTH, HEIGHT))
        self.clock = pygame.time.Clock()

        # FreeSerif carries the chess glyphs; DejaVu is the readable UI face.
        piece_path = pygame.font.match_font("freeserif") or \
                     pygame.font.match_font("dejavusans")
        self.f_piece = pygame.font.Font(piece_path, int(SQ * 0.78))
        self.f_ui = pygame.font.Font(pygame.font.match_font("dejavusans"), 15)
        self.f_small = pygame.font.Font(pygame.font.match_font("dejavusans"), 13)
        self.f_mono = pygame.font.Font(pygame.font.match_font("dejavusansmono"), 13)
        self.f_tiny = pygame.font.Font(pygame.font.match_font("dejavusansmono"), 11)
        self.f_big = pygame.font.Font(pygame.font.match_font("dejavuserif"), 20)

        self.engine = EngineWorker(args.engine, args.hash, args.movetime)
        self.human = chess.WHITE if args.color == "white" else chess.BLACK
        self.flipped = (self.human == chess.BLACK)

        self.board = chess.Board()
        self.selected = None
        self.legal_targets: set[int] = set()
        self.dragging = False
        self.drag_pos = (0, 0)
        self.last_move: chess.Move | None = None
        self.status = "Your move" if self.human == chess.WHITE else "Engine to move"
        self.info = {}
        self.score_cp = 0
        self.pending_promo: tuple[int, int] | None = None
        self.press_sq: int | None = None
        self.redo_stack: list[chess.Move] = []

        if self.board.turn != self.human:
            self.ask_engine()

    # ------------------------------------------------------------ helpers

    def sq_to_xy(self, square: int):
        f, r = chess.square_file(square), chess.square_rank(square)
        col = 7 - f if self.flipped else f
        row = r if self.flipped else 7 - r
        return MARGIN + col * SQ, MARGIN + row * SQ

    def xy_to_sq(self, x, y):
        if not (MARGIN <= x < MARGIN + BOARD and MARGIN <= y < MARGIN + BOARD):
            return None
        col, row = (x - MARGIN) // SQ, (y - MARGIN) // SQ
        f = 7 - col if self.flipped else col
        r = row if self.flipped else 7 - row
        return chess.square(int(f), int(r))

    def ask_engine(self):
        if not self.board.is_game_over() and not self.engine.busy:
            self.status = "Thinking..."
            self.engine.request(self.board)

    def push(self, move: chess.Move):
        self.board.push(move)
        self.last_move = move
        self.redo_stack.clear()
        self.selected = None
        self.legal_targets.clear()
        if self.board.is_game_over():
            self.status = self.result_text()
        elif self.board.turn != self.human:
            self.ask_engine()
        else:
            self.status = "Your move"

    def result_text(self):
        if self.board.is_checkmate():
            winner = "You win" if self.board.turn != self.human else "Engine wins"
            return f"Checkmate - {winner}"
        if self.board.is_stalemate():
            return "Draw - stalemate"
        if self.board.is_insufficient_material():
            return "Draw - insufficient material"
        if self.board.can_claim_threefold_repetition():
            return "Draw - repetition"
        if self.board.can_claim_fifty_moves():
            return "Draw - fifty-move rule"
        return "Game over"

    def takeback(self):
        # Undo a full move pair so it stays the human's turn.
        if self.engine.busy or not self.board.move_stack:
            return
        n = 2 if len(self.board.move_stack) >= 2 else 1
        for _ in range(n):
            self.redo_stack.append(self.board.pop())
        self.last_move = self.board.move_stack[-1] if self.board.move_stack else None
        self.selected = None
        self.legal_targets.clear()
        self.status = "Your move"

    def redo(self):
        if self.engine.busy or not self.redo_stack:
            return
        for _ in range(min(2, len(self.redo_stack))):
            self.board.push(self.redo_stack.pop())
        self.last_move = self.board.move_stack[-1] if self.board.move_stack else None

    def new_game(self):
        if self.engine.busy:
            return
        self.board.reset()
        self.last_move = None
        self.selected = None
        self.legal_targets.clear()
        self.redo_stack.clear()
        self.info = {}
        self.score_cp = 0
        self.status = "Your move" if self.human == chess.WHITE else "Engine to move"
        if self.board.turn != self.human:
            self.ask_engine()

    # ------------------------------------------------------------ input

    def click(self, x, y, down: bool):
        if self.pending_promo:
            self.choose_promotion(x, y)
            return
        if self.board.turn != self.human or self.board.is_game_over():
            return
        sq = self.xy_to_sq(x, y)
        if sq is None:
            return

        if self.selected is None:
            piece = self.board.piece_at(sq)
            if piece and piece.color == self.human:
                self.select(sq, down)
            return

        if sq == self.selected:
            self.selected = None
            self.legal_targets.clear()
            return

        candidates = [m for m in self.board.legal_moves
                      if m.from_square == self.selected and m.to_square == sq]
        if candidates:
            if any(m.promotion for m in candidates):
                self.pending_promo = (self.selected, sq)
                self.selected = None
                self.legal_targets.clear()
            else:
                self.push(candidates[0])
            return

        piece = self.board.piece_at(sq)
        if piece and piece.color == self.human:
            self.select(sq, down)
        else:
            self.selected = None
            self.legal_targets.clear()

    def select(self, sq, down):
        self.selected = sq
        self.legal_targets = {m.to_square for m in self.board.legal_moves
                              if m.from_square == sq}
        self.dragging = down

    def choose_promotion(self, x, y):
        opts = [chess.QUEEN, chess.ROOK, chess.BISHOP, chess.KNIGHT]
        bx = MARGIN + BOARD // 2 - 2 * SQ
        by = MARGIN + BOARD // 2 - SQ // 2
        for i, pt in enumerate(opts):
            if pygame.Rect(bx + i * SQ, by, SQ, SQ).collidepoint(x, y):
                frm, to = self.pending_promo
                self.pending_promo = None
                mv = chess.Move(frm, to, promotion=pt)
                if mv in self.board.legal_moves:
                    self.push(mv)
                return
        self.pending_promo = None

    # ------------------------------------------------------------ drawing

    def draw_piece(self, piece: chess.Piece, cx: int, cy: int):
        glyph = GLYPH[piece.piece_type]
        # Halo first: white pieces need a dark edge on cream squares, black
        # pieces need a light one on walnut. Without it both vanish.
        outline = (20, 17, 14) if piece.color == chess.WHITE else (232, 222, 202)
        body = (250, 246, 236) if piece.color == chess.WHITE else (28, 24, 20)
        for dx, dy in ((-2, 0), (2, 0), (0, -2), (0, 2),
                       (-1, -1), (1, 1), (-1, 1), (1, -1)):
            s = self.f_piece.render(glyph, True, outline)
            self.screen.blit(s, s.get_rect(center=(cx + dx, cy + dy)))
        s = self.f_piece.render(glyph, True, body)
        self.screen.blit(s, s.get_rect(center=(cx, cy)))

    def draw_board(self):
        for sq in chess.SQUARES:
            x, y = self.sq_to_xy(sq)
            light = (chess.square_file(sq) + chess.square_rank(sq)) % 2 == 1
            pygame.draw.rect(self.screen, CREAM if light else WALNUT, (x, y, SQ, SQ))
            if self.last_move and sq in (self.last_move.from_square,
                                         self.last_move.to_square):
                hl = pygame.Surface((SQ, SQ), pygame.SRCALPHA)
                hl.fill((201, 162, 39, 90))
                self.screen.blit(hl, (x, y))
            if sq == self.selected:
                pygame.draw.rect(self.screen, BRASS, (x, y, SQ, SQ), 3)

        if self.board.is_check():
            ksq = self.board.king(self.board.turn)
            if ksq is not None:
                x, y = self.sq_to_xy(ksq)
                hl = pygame.Surface((SQ, SQ), pygame.SRCALPHA)
                pygame.draw.circle(hl, (217, 83, 79, 120),
                                   (SQ // 2, SQ // 2), SQ // 2 - 3)
                self.screen.blit(hl, (x, y))

        for i in range(8):
            f = 7 - i if self.flipped else i
            t = self.f_small.render("abcdefgh"[f], True, CREAM_DIM)
            self.screen.blit(t, (MARGIN + i * SQ + SQ - 13, MARGIN + BOARD + 3))
            r = i if self.flipped else 7 - i
            t = self.f_small.render(str(r + 1), True, CREAM_DIM)
            self.screen.blit(t, (MARGIN - 15, MARGIN + i * SQ + 4))

        for sq in self.legal_targets:
            x, y = self.sq_to_xy(sq)
            hint = pygame.Surface((SQ, SQ), pygame.SRCALPHA)
            occupied = self.board.piece_at(sq) is not None
            if occupied:
                pygame.draw.circle(hint, (25, 22, 20, 90),
                                   (SQ // 2, SQ // 2), SQ // 2 - 3, 6)
            else:
                pygame.draw.circle(hint, (25, 22, 20, 90), (SQ // 2, SQ // 2), 11)
            self.screen.blit(hint, (x, y))

        for sq in chess.SQUARES:
            piece = self.board.piece_at(sq)
            if not piece or (self.dragging and sq == self.selected):
                continue
            x, y = self.sq_to_xy(sq)
            self.draw_piece(piece, x + SQ // 2, y + SQ // 2)

        if self.dragging and self.selected is not None:
            piece = self.board.piece_at(self.selected)
            if piece:
                self.draw_piece(piece, *self.drag_pos)

    def draw_evalbar(self):
        x, y = MARGIN + BOARD + 12, MARGIN
        pygame.draw.rect(self.screen, DARK_BAR, (x, y, BAR_W, BOARD))
        cp = self.score_cp
        pct = 1 / (1 + pow(10, -cp / 400.0))
        pct = max(0.02, min(0.98, pct))
        h = int(BOARD * ((1 - pct) if self.flipped else pct))
        pygame.draw.rect(self.screen, WHITE_BAR, (x, y + BOARD - h, BAR_W, h))
        pygame.draw.rect(self.screen, BRASS, (x, y + BOARD - h - 2, BAR_W, 2))
        pygame.draw.rect(self.screen, LINE, (x, y, BAR_W, BOARD), 1)
        label = f"{cp/100:+.1f}" if abs(cp) < 9000 else ("#" if cp > 0 else "-#")
        # Put the label on whichever half of the bar actually has room, and
        # colour it for that half. A fixed position ends up straddling the
        # boundary at lopsided evals and becomes unreadable.
        if h > 34:
            ly, lcol = y + BOARD - 15, (74, 68, 56)      # on the white portion
        else:
            ly, lcol = y + BOARD - h - 15, CREAM         # just above it, on dark
        t = self.f_tiny.render(label, True, lcol)
        self.screen.blit(t, t.get_rect(center=(x + BAR_W // 2, ly)))

    def draw_panel(self):
        x, y = MARGIN + BOARD + 12 + BAR_W + 14, MARGIN
        pygame.draw.rect(self.screen, PANEL, (x, y, PANEL_W, BOARD))
        pygame.draw.rect(self.screen, LINE, (x, y, PANEL_W, BOARD), 1)

        self.screen.blit(self.f_big.render(self.engine.name[:24], True, BRASS),
                         (x + 14, y + 12))
        sub = (f"you play {'White' if self.human == chess.WHITE else 'Black'}"
               f"  -  {self.engine.movetime:g}s/move")
        self.screen.blit(self.f_small.render(sub, True, CREAM_DIM), (x + 14, y + 40))

        yy = y + 68
        nodes, nps = self.info.get("nodes", 0), self.info.get("nps", 0)
        for k, v in (("depth", str(self.info.get("depth", "-"))),
                     ("eval", f"{self.score_cp/100:+.2f}"),
                     ("nodes", f"{nodes:,}" if nodes else "-"),
                     ("nps", f"{nps:,}" if nps else "-")):
            self.screen.blit(self.f_small.render(k, True, CREAM_DIM), (x + 14, yy))
            t = self.f_mono.render(v, True, CREAM)
            self.screen.blit(t, (x + PANEL_W - 14 - t.get_width(), yy))
            yy += 21

        pygame.draw.line(self.screen, LINE, (x + 12, yy + 6), (x + PANEL_W - 12, yy + 6))
        yy += 16

        moves, tmp = [], chess.Board()
        for m in self.board.move_stack:
            moves.append(tmp.san(m))
            tmp.push(m)
        rows_total = (len(moves) + 1) // 2
        visible = max(1, (BOARD - (yy - y) - 12) // 20)
        for i in range(max(0, rows_total - visible), rows_total):
            self.screen.blit(self.f_mono.render(f"{i+1:>3}.", True, CREAM_DIM),
                             (x + 14, yy))
            w = moves[i * 2] if i * 2 < len(moves) else ""
            b = moves[i * 2 + 1] if i * 2 + 1 < len(moves) else ""
            self.screen.blit(self.f_ui.render(w, True, CREAM), (x + 58, yy - 2))
            self.screen.blit(self.f_ui.render(b, True, CREAM), (x + 165, yy - 2))
            yy += 20

    def draw_statusbar(self):
        y = MARGIN + BOARD + MARGIN - 4
        pygame.draw.rect(self.screen, PANEL, (0, y, WIDTH, HEIGHT - y))
        col = BRASS if self.engine.busy else CREAM
        if "You win" in self.status:
            col = GOOD
        self.screen.blit(self.f_ui.render(self.status, True, col), (MARGIN, y + 12))
        hint = "n new game    f flip    <- takeback    -> redo    q quit"
        t = self.f_small.render(hint, True, CREAM_DIM)
        self.screen.blit(t, (WIDTH - MARGIN - t.get_width(), y + 14))

    def draw_promo(self):
        if not self.pending_promo:
            return
        veil = pygame.Surface((WIDTH, HEIGHT), pygame.SRCALPHA)
        veil.fill((10, 9, 8, 170))
        self.screen.blit(veil, (0, 0))
        bx = MARGIN + BOARD // 2 - 2 * SQ
        by = MARGIN + BOARD // 2 - SQ // 2
        pygame.draw.rect(self.screen, PANEL_2, (bx - 8, by - 36, SQ * 4 + 16, SQ + 48))
        pygame.draw.rect(self.screen, BRASS, (bx - 8, by - 36, SQ * 4 + 16, SQ + 48), 2)
        self.screen.blit(self.f_ui.render("Promote to", True, CREAM), (bx, by - 28))
        for i, pt in enumerate([chess.QUEEN, chess.ROOK, chess.BISHOP, chess.KNIGHT]):
            r = pygame.Rect(bx + i * SQ, by, SQ, SQ)
            pygame.draw.rect(self.screen, CREAM if i % 2 == 0 else WALNUT, r)
            self.draw_piece(chess.Piece(pt, self.human), r.centerx, r.centery)

    def draw(self):
        self.screen.fill(INK)
        self.draw_board()
        self.draw_evalbar()
        self.draw_panel()
        self.draw_statusbar()
        self.draw_promo()
        pygame.display.flip()

    # ------------------------------------------------------------ loop

    def handle_engine(self):
        got = self.engine.poll()
        if not got:
            return
        kind, move, info = got
        if kind == "error":
            self.status = f"Engine error: {info}"
        elif move is not None:
            self.info = info or {}
            sc = self.info.get("score")
            if sc is not None:
                # Always display from White's perspective, like every GUI does.
                self.score_cp = sc.white().score(mate_score=10000)
            self.push(move)

    def run(self):
        running = True
        while running:
            for ev in pygame.event.get():
                if ev.type == pygame.QUIT:
                    running = False
                elif ev.type == pygame.MOUSEBUTTONDOWN and ev.button == 1:
                    self.drag_pos = ev.pos
                    self.press_sq = self.xy_to_sq(*ev.pos)
                    self.click(*ev.pos, down=True)
                elif ev.type == pygame.MOUSEMOTION:
                    self.drag_pos = ev.pos
                elif ev.type == pygame.MOUSEBUTTONUP and ev.button == 1:
                    was_dragging = self.dragging
                    self.dragging = False
                    up_sq = self.xy_to_sq(*ev.pos)
                    # Releasing on the square you pressed is a CLICK (keep the
                    # selection so the next click can move). Releasing
                    # elsewhere is a DRAG (complete the move).
                    if was_dragging and up_sq is not None and up_sq != self.press_sq:
                        self.click(*ev.pos, down=False)
                elif ev.type == pygame.KEYDOWN:
                    if ev.key in (pygame.K_q, pygame.K_ESCAPE):
                        running = False
                    elif ev.key == pygame.K_f:
                        self.flipped = not self.flipped
                    elif ev.key == pygame.K_n:
                        self.new_game()
                    elif ev.key == pygame.K_LEFT:
                        self.takeback()
                    elif ev.key == pygame.K_RIGHT:
                        self.redo()

            self.handle_engine()
            self.draw()
            self.clock.tick(60)

        self.engine.close()
        pygame.quit()


def main():
    ap = argparse.ArgumentParser(description="Play against the Nitro engine.")
    ap.add_argument("--engine", default="./build/nitro")
    ap.add_argument("--color", choices=["white", "black"], default="white")
    ap.add_argument("--movetime", type=float, default=1.0,
                    help="seconds the engine thinks per move")
    ap.add_argument("--hash", type=int, default=256)
    args = ap.parse_args()
    Game(args).run()


if __name__ == "__main__":
    main()
