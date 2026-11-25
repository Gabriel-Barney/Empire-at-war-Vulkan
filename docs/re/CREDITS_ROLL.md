# The credits roll and the Credits_* GameConstants

Retail FoC x64 `StarWarsG.exe`, read in the Ghidra export and the disassembly (#426, 2026-11-24). Tiers: **[H]** read
directly, **[M]** inferred with good support, **[L]** guess. Addresses are image offsets from 0x140000000.

GameConstants is a static object at `0xb15800`. A scan of every function found only these readers of the Credits_*
fields:
- the roll setup `0x44e5a0`: Spacing, Font, Font_Size, the colours and the logos;
- the per-frame scroll `0x44e020`: Scroll_Rate and Margin.

Nothing reads `Credits_Rows`.

## How the roll starts [H]

- **The button.** `IDC_MENU_CREDITS` (main-menu handler `0xc6590`) stops any movie (`0x44f130`), hides the menu, then
  plays the Movies.xml movie **`Credits`**: `0x44d920("Credits", loop = 1, on_end = 0xc58e0, skippable = 1)`.
- **The movie.** The shipped entry is `credits_loop.bik`, with Has_Credits, Overlay and Movie_Start_Frame 1.
- **The roll.** Has_Credits makes the player call the roll setup (`0x44dd90`). The roll is not a screen of its own: it
  draws over the movie.
- **The backdrop** is that Bink movie, looped and full screen with a white tint. It is not black.
- **The music.** The setup plays `Game_Credits_Music_Event` in music mode 0xe (`0x44ef4a`-`0x44f01e`).

## Layout [H]

Positions are fractions of the screen, with y down and 0 at the top. H is the screen height in pixels.

- **Pitch:** `P = floor(Credits_Spacing x H) / H`, snapped to whole pixels (`0x44e6ff`).
- **One item per record:** each DAT record k, in file order, becomes one text item. `[TBL]` blanks count: their text is
  emptied but they still take a pitch.
- **Positions:** item k's top edge is at `1 + k x P`, so line 0 starts at the bottom edge. Each item is centred
  horizontally.
- **Header colours:** a record whose key **contains** `HEADER` takes the header colours (`0x44eab6`; the call is strstr
  [M]). Every other record takes the Top / Bottom colours. Each pair is a vertical gradient.
- **Font:** one font for headers and names, `Credits_Font` at the integer size `Credits_Font_Size`.
- **Logos:**
  - **Placement:** up to three TextureRects follow the text, each placed at `1 + N x P + Logo_i_Y_Offset`.
    The offsets do not add up.
  - **Horizontal:** logo 1 is at the left, logo 2 in the centre and logo 3 at the right.
  - **Size:** `Logo_i_Width` wide; the height keeps the texture's aspect.
  - **Missing textures:** a logo whose texture does not load is skipped.

## Per frame, `0x44e020` [H]

- **Accumulate:** `acc += Credits_Scroll_Rate x dt`, in seconds.
- **Move:** when `acc <= 1/H`, then `acc += 1/H` and every item moves up one pixel. Over time the roll has moved
  `n(t) = min(frames, floor(-rate x H x t) + 2)` pixels. The unit is screen heights per second, negative is up, and the
  roll moves **at most one pixel per frame**.
- **Margin:** `Credits_Margin` fades **only the logos**, with a smoothstep over the bands `[m, 2m]` and `[1-2m, 1-m]`.
  It is 0 outside `[m, 1-m]`. The text lines never fade.

## End and skip [H]

- **End:** the roll ends (`0x44d780`) when every item, blanks and logos included, has its top edge at or above the top
  of the screen: the largest top-y is <= 0, so the last item decides. The stop (`0x44f130`) closes the movie, the roll and the music and brings the main menu back.
- **Skip:** the key handler `0x61fe0` stops a skippable movie only on **Esc or Space**. Every other key is swallowed.
  The mouse path was not traced.

## Values

- **Which Scroll_Rate wins.** config.meg's GameConstants.xml has `-0.05`. Patch2.meg's copy comments out `-0.05` and
  sets **`-0.10`**. Patch2.meg replaces the whole file, so `-0.10` applies; no shipped file lists two live tags.
- **Retail defaults** (ctor `0x2c91c0`):
  - Spacing 0.01, Scroll_Rate -0.01, Margin 0.
  - Font `Arial Unicode MS`, size 10.
  - All four colours white.
  - Logo width and height 0.1, Y offset 0.5.

## Port

`engine/ui/src/credits_roll.cpp` and `engine/apps/game/app/app_options.cpp` follow this note for:
- the floored pitch;
- the one-pixel steps toward `credits_scroll_target_px`;
- unfaded text;
- the end rule;
- the header match;
- the defaults;
- the skip keys;
- the looping `Credits` movie, whose Has_Credits starts the credits music [M for the port's wiring];
- the logos (#458): `credits_logo_rect`, `credits_logo_alpha`, and the end test with them.

Still `[port]`:
- **Click to skip:** a click skips too.
- **No movie:** without the movie the roll runs over black.
- **Logo textures** (docs/DECISIONS.md "Front-end and skirmish choices"): they load through the GUI image pages after the
  roll starts, so a logo takes part once its texture has resolved, and keeps its size from then on.

## Open

- Whether a mouse click skips the movie and roll.
- Whether `Credits_Font_Size` scales with the resolution [L].
- How the renderer applies the gradient colours [M].
