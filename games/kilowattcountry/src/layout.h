/* Kilowatt Country: where things sit on the table (world units, y up, the camera looks from +z). The night map's
 * texture (tools/make-textures.mjs) covers the board's top face exactly; city spots come from kc_data.h. */
#ifndef KILOWATTCOUNTRY_LAYOUT_H
#define KILOWATTCOUNTRY_LAYOUT_H

#include "rules.h"
#include "three.h"

#define LY_MAP_X0 (-6.75f)
#define LY_MAP_W 13.5f
#define LY_MAP_Z0 (-6.4f)
#define LY_MAP_D 9.0f
#define LY_BOARD_Y 0.18f
#define LY_CARD_W 1.15f
#define LY_CARD_D 1.6f
#define LY_FUEL_X0 7.45f
#define LY_FUEL_W 5.3f
#define LY_TRAY_Z 5.05f
#define LY_TRAY_W 5.0f
#define LY_TRAY_D 2.5f
#define LY_TRAY_Y 0.02f

static inline t3_vec3 ly_city(int c) {
  return (t3_vec3){ LY_MAP_X0 + kc_cities[c].x * LY_MAP_W, LY_BOARD_Y, LY_MAP_Z0 + kc_cities[c].z * LY_MAP_D };
}
/* a city's pieces (plinth, rooms, houses) at this scale: small enough that real cities close together stay apart */
#define LY_CITY_S 0.72f
/* a house's room in a city: three around the city's centre */
static inline t3_vec3 ly_room(int c, int k) {
  t3_vec3 p = ly_city(c);
  static const float dx[3] = { -0.21f * LY_CITY_S, 0.21f * LY_CITY_S, 0.0f }, dz[3] = { 0.12f * LY_CITY_S, 0.12f * LY_CITY_S, -0.23f * LY_CITY_S };
  return (t3_vec3){ p.x + dx[k], p.y + 0.06f, p.z + dz[k] };
}
/* the plant market: two rows of four on the left (in step 3 all six can be bought) */
static inline t3_vec3 ly_market(int slot) {
  return (t3_vec3){ -12.0f + (float)(slot % 4) * 1.3f, LY_BOARD_Y - 0.15f, -4.95f + (float)(slot / 4) * 2.45f };
}
static inline t3_vec3 ly_deck(void) { return (t3_vec3){ -12.0f, 0.03f, -0.1f }; }
/* set-aside plants (four at most): a row beside the deck, at LY_ASIDE_SCALE, large enough to read from the couch */
#define LY_ASIDE_SCALE 0.8f
static inline t3_vec3 ly_aside(int i) { return (t3_vec3){ -10.55f + (float)i * 0.98f, 0.03f, -0.1f }; }
/* a fuel cube's cell, slot 0 the cheapest: coal, oil, garbage, uranium rows top to bottom */
static inline float ly_fuel_row_z(int f) { return -5.45f + (float)f * 1.4f; }
static inline t3_vec3 ly_fuel_slot(int f, int j) {
  float x = f == KC_URANIUM ? LY_FUEL_X0 + 0.22f + (float)j * (LY_FUEL_W / 12.0f)
                            : LY_FUEL_X0 + 0.15f + (float)(j / 3) * (LY_FUEL_W / 8.0f) + (float)(j % 3) * 0.2f;
  return (t3_vec3){ x, 0.05f, ly_fuel_row_z(f) + 0.14f };
}
/* the centre of a price cell (labels) */
static inline t3_vec3 ly_fuel_cell(int f, int cell) {
  float w = f == KC_URANIUM ? LY_FUEL_W / 12.0f : LY_FUEL_W / 8.0f;
  return (t3_vec3){ LY_FUEL_X0 + (float)cell * w + w * 0.5f, 0.05f, ly_fuel_row_z(f) };
}
/* the players' trays along the near edge */
static inline float ly_tray_x(int seat, int players) { return ((float)seat - (float)(players - 1) * 0.5f) * (LY_TRAY_W + 0.35f); }
static inline t3_vec3 ly_tray(int seat, int players) { return (t3_vec3){ ly_tray_x(seat, players), LY_TRAY_Y, LY_TRAY_Z }; }
static inline t3_vec3 ly_tray_card(int seat, int k, int players) {
  return (t3_vec3){ ly_tray_x(seat, players) - 1.3f + (float)k * 1.3f, LY_TRAY_Y + 0.03f, LY_TRAY_Z - 0.15f };
}
/* fuel stored on a plant card: a row along the card's near edge */
static inline t3_vec3 ly_card_fuel(t3_vec3 card, int i) {
  return (t3_vec3){ card.x - 0.45f + (float)(i % 4) * 0.3f, card.y + 0.06f, card.z + 1.0f + (float)(i / 4) * 0.24f };
}

#endif
