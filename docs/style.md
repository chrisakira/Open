# Interface style

The character popup is the approved visual reference for Open's menus and
popups. New interfaces should feel like part of the same game: **rustic and
medieval**, with dark wood and leather tones, parchment-colored text, and
restrained brass accents. Aim for a practical, readable character sheet, not a
modern glossy dashboard or an ornate fantasy frame that competes with the data.

The current reference implementation is `scripts/character_popup.gd`. This
document sets the direction for future interfaces; it does not imply that a
shared theme or reusable menu components already exist.

## Palette

Use these colors as the starting palette for popups and menus. The hex values
are RGB; transparency is applied separately.

| Use | Color | Reference |
|---|---|---|
| Main panel | `#241B16` | Dark, warm brown |
| Header | `#493326` | Lighter leather brown |
| Frame and control borders | `#806343` | Muted brass |
| Body text | `#E0D0AE` | Warm parchment |
| Main title and character names | `#F4E6C6` | Light parchment |
| Numeric values | `#F0D3A0` | Warm, distinct from labels |
| Accent | `#D0A96B` | Brass highlight |
| Secondary text and group headings | `#BD9862` | Muted gold |
| Section heading | `#E3C590` | Light gold |
| Section rule | `#6E5539` | Subtle brown-gold |
| Cast band and bar track | `#2E231C` | Dark warm brown behind a plaque |
| Vertical divider | `#675038` | Subtle brown-gold |
| Footer hint | `#9C886D` | Recessive, but readable |

Use accents sparingly for hierarchy and interaction feedback. Maintain enough
contrast for text against the panel and against the game world visible behind
it; color alone must not be the only way to understand a control or value.

## Panels and layout

- Use a dark, flat panel with a thin muted-brass frame and a soft dark shadow.
  The character popup uses a 2-pixel border, 4-pixel inner frame margin and a
  12-pixel shadow. Avoid shiny gradients, bright saturated colors and excessive
  decoration.
- Floating popups and in-game menus use **90% opacity**, as on the character
  popup. Apply this to the whole surface, including its contents, rather than
  silently changing the setting per element. Check readability against the
  world; if a particular interface needs different behavior, make that an
  explicit design decision rather than obscuring important information.
- Give the header a lighter brown surface and a thin brass bottom rule. Keep
  the title prominent, secondary context smaller, and the close button at the
  right. A small gold emblem is appropriate where it supports the layout; it is
  decoration, not gameplay information.
- Use generous inner spacing and clear sections. The reference uses 24-pixel
  horizontal body margins, 22 pixels above and 18 below, and 24 pixels between
  columns. These are reference values, not a fixed size for every menu.
- Use uppercase section and group headings in warm gold, with thin rules or
  dividers where useful. Keep decoration quieter than the actual content.
- For name/value lists, align names left and values right in consistent columns.
  The character sheet uses two columns: attributes on the left and grouped,
  vertically scrollable skills on the right. Other menus should choose the
  layout their content needs rather than forcing this arrangement everywhere.
- Support the project viewport and smaller windows: keep controls reachable,
  text readable and long content scrollable instead of clipping it. Do not
  assume the character popup's `760 × 520` minimum is right for every menu.

## Typography

- Use a readable serif face. The current system-font fallback order is
  `Georgia`, `DejaVu Serif`, `Liberation Serif`, then `serif`; these are system
  fonts, not bundled assets, so the rendered face can differ between machines.
- The character popup uses approximately 24 pixels for the title, 17 for
  section headings, 16 for body labels and values, 13 for group headings, and
  11 for secondary header and footer text. Preserve this hierarchy and adjust
  sizes where needed for legibility.
- Prefer plain, easily scanned text and numbers. Do not sacrifice readability
  for an overly decorative medieval typeface.

## Controls and interaction

- Style buttons with dark brown fills, thin brass borders and parchment text.
  Provide visible hover and pressed states; the reference close button uses
  `#382B21` normally, `#62472F` on hover and `#241B16` when pressed.
- Use narrow scrollbars in the same palette: `#2C211A` for the track,
  `#8C6B45` for the thumb, `#B08A58` on hover and `#D0A96B` when pressed.
  Keep the scrollbar and mouse-wheel scrolling usable, not merely decorative.
- Movable floating popups should offer a clear header drag area without making
  buttons, scrolling or other controls difficult to use. The character popup
  centers on first opening, moves by its header and keeps its dragged position
  when closed and reopened.
- Popup controls must consume their own pointer input. Clicks inside a popup
  must not trigger the world underneath; clicks outside a non-modal popup must
  continue to work. The character popup does not pause the world or interrupt
  an existing movement order.
- Keep close controls and shortcuts clear and consistent with the interface's
  purpose. On the character popup, **I** toggles the sheet, key repeats do not
  toggle it, and **×** closes it. Do not assume that every future menu uses I
  or must be non-modal; those behaviors depend on its design.

## Implementation and verification

Use Godot controls, theme overrides or a future shared Godot theme for
presentation. Gameplay values stay in C++ and are exposed through the
GDExtension; a menu displays state and sends intent rather than owning the
simulation. Do not add gameplay rules or make the visual style a source of
truth for character data.

When adding or changing a menu, check its appearance at the normal project
window size and a smaller viewport. Verify readable text, contrast, scrolling,
hover and pressed states, close behavior, and pointer isolation. For floating
popups, also check dragging and that existing world behavior continues when it
should. The headless regression coverage for the reference popup is in
`scripts/tests/character_popup_test.gd`.

Drawn chrome — a panel's frame, a plaque's cast band, a card's tail — is worth
exposing as a `paint_layers` function that `_draw` submits unchanged. Nothing
rasterises under `--headless`, so the layer list (its order, its insets, its
colors) is the only way a suite can assert that the frame is outside the field
and the tail hangs below the card, instead of trusting that the right constants
were handed to the right `draw_rect`.

A style box is already the drawn result, so each popup suite reads it back
(`_check_frame`): the panel's field and border colours, the two-pixel border on
all four sides, and the surface's 90% opacity. Without that pin the palette can
drift one panel at a time — the attack, weapon, equipment, technique and
workbench panels all once sat at 92–94% while this document said 90%.
