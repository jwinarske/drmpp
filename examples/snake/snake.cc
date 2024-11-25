/*
 * Copyright (c) 2024 The drmpp Contributors
 * Copyright (c) 2022, Dylam De La Torre <dyxel04@gmail.com>
 *
 * SPDX-License-Identifier: Zlib
 */

#include "snake.h"

#include <climits>
#include <cstdlib>
#include <cstring>

#define THREE_BITS 0x7U  // ~CHAR_MAX >> (CHAR_BIT - SNAKE_CELL_MAX_BITS)
#define SHIFT(x, y) (((x) + ((y) * SNAKE_GAME_WIDTH)) * SNAKE_CELL_MAX_BITS)

static void put_cell_at_(SnakeContext* ctx,
                         const char x,
                         const char y,
                         const SnakeCell ct) {
  const int shift = SHIFT(x, y);
  const int adjust = shift % CHAR_BIT;
  unsigned char* const pos = ctx->cells + (shift / CHAR_BIT);
  unsigned short range;
  memcpy(&range, pos, sizeof(range));
  range &= ~(THREE_BITS << adjust);  // clear bits
  range |= (ct & THREE_BITS) << adjust;
  memcpy(pos, &range, sizeof(range));
}

static int are_cells_full_(const SnakeContext* ctx) {
  return ctx->occupied_cells == SNAKE_GAME_WIDTH * SNAKE_GAME_HEIGHT;
}

static void new_food_pos_(SnakeContext* ctx) {
  std::uniform_int_distribution<char> dist_x(0, SNAKE_GAME_WIDTH - 1);
  std::uniform_int_distribution<char> dist_y(0, SNAKE_GAME_HEIGHT - 1);

  for (;;) {
    auto x = dist_x(ctx->rng);
    auto y = dist_y(ctx->rng);
    if (snake_cell_at(ctx, x, y) == SNAKE_CELL_NOTHING) {
      put_cell_at_(ctx, x, y, SNAKE_CELL_FOOD);
      ctx->food = {x, y};
      break;
    }
  }
}

void snake_initialize(SnakeContext* ctx) {
  memset(ctx->cells, 0, sizeof(ctx->cells));
  ctx->head_xpos = ctx->tail_xpos = SNAKE_GAME_WIDTH / 2;
  ctx->head_ypos = ctx->tail_ypos = SNAKE_GAME_HEIGHT / 2;
  ctx->next_dir = SNAKE_DIR_RIGHT;
  ctx->inhibit_tail_step = 4;
  ctx->occupied_cells = 4;
  --ctx->occupied_cells;
  put_cell_at_(ctx, ctx->tail_xpos, ctx->tail_ypos, SNAKE_CELL_SRIGHT);
  ctx->snake.clear();
  ctx->snake.emplace_back(ctx->head_xpos, ctx->head_ypos);
  ctx->rng.seed(std::random_device{}());  // Seed the random number generator
  for (int i = 0; i < 4; i++) {
    new_food_pos_(ctx);
    ++ctx->occupied_cells;
  }
}

void snake_redir(SnakeContext* ctx, const SnakeDirection dir) {
  if (const SnakeCell ct = snake_cell_at(ctx, ctx->head_xpos, ctx->head_ypos);
      (dir == SNAKE_DIR_RIGHT && ct != SNAKE_CELL_SLEFT) ||
      (dir == SNAKE_DIR_UP && ct != SNAKE_CELL_SDOWN) ||
      (dir == SNAKE_DIR_LEFT && ct != SNAKE_CELL_SRIGHT) ||
      (dir == SNAKE_DIR_DOWN && ct != SNAKE_CELL_SUP)) {
    ctx->next_dir = dir;
  }
}

static void wrap_around_(char* val, const char max) {
  if (*val < 0) {
    *val = static_cast<char>(max - 1);
  }
  if (*val > max - 1) {
    *val = 0;
  }
}

void snake_step(SnakeContext* ctx) {
  const auto dir_as_cell = static_cast<SnakeCell>(ctx->next_dir + 1);
  SnakeCell ct;
  // Move tail forward
  if (--ctx->inhibit_tail_step == 0) {
    ++ctx->inhibit_tail_step;
    ct = snake_cell_at(ctx, ctx->tail_xpos, ctx->tail_ypos);
    put_cell_at_(ctx, ctx->tail_xpos, ctx->tail_ypos, SNAKE_CELL_NOTHING);
    switch (ct) {
      case SNAKE_CELL_SRIGHT:
        ctx->tail_xpos++;
        break;
      case SNAKE_CELL_SUP:
        ctx->tail_ypos--;
        break;
      case SNAKE_CELL_SLEFT:
        ctx->tail_xpos--;
        break;
      case SNAKE_CELL_SDOWN:
        ctx->tail_ypos++;
        break;
      default:
        break;
    }
    wrap_around_(&ctx->tail_xpos, SNAKE_GAME_WIDTH);
    wrap_around_(&ctx->tail_ypos, SNAKE_GAME_HEIGHT);
    ctx->snake.erase(ctx->snake.begin());
  }
  // Move head forward
  const char prev_xpos = ctx->head_xpos;
  const char prev_ypos = ctx->head_ypos;
  switch (ctx->next_dir) {
    case SNAKE_DIR_RIGHT:
      ++ctx->head_xpos;
      break;
    case SNAKE_DIR_UP:
      --ctx->head_ypos;
      break;
    case SNAKE_DIR_LEFT:
      --ctx->head_xpos;
      break;
    case SNAKE_DIR_DOWN:
      ++ctx->head_ypos;
      break;
    default:
      break;
  }
  wrap_around_(&ctx->head_xpos, SNAKE_GAME_WIDTH);
  wrap_around_(&ctx->head_ypos, SNAKE_GAME_HEIGHT);
  // Collisions
  ct = snake_cell_at(ctx, ctx->head_xpos, ctx->head_ypos);
  if (ct != SNAKE_CELL_NOTHING && ct != SNAKE_CELL_FOOD) {
    snake_initialize(ctx);
    return;
  }
  put_cell_at_(ctx, prev_xpos, prev_ypos, dir_as_cell);
  put_cell_at_(ctx, ctx->head_xpos, ctx->head_ypos, dir_as_cell);
  ctx->snake.emplace_back(ctx->head_xpos, ctx->head_ypos);
  if (ct == SNAKE_CELL_FOOD) {
    if (are_cells_full_(ctx)) {
      snake_initialize(ctx);
      return;
    }
    new_food_pos_(ctx);
    ++ctx->inhibit_tail_step;
    ++ctx->occupied_cells;
  }
}

SnakeCell snake_cell_at(const SnakeContext* ctx, const char x, const char y) {
  const int shift = SHIFT(x, y);
  unsigned short range;
  memcpy(&range, ctx->cells + (shift / CHAR_BIT), sizeof(range));
  return static_cast<SnakeCell>((range >> (shift % CHAR_BIT)) & THREE_BITS);
}