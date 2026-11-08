# Lessons

## Tiny touch screens (1.47", ~10 px/mm)
- A fingertip covers ~100+ px. Never put a control under the finger: use relative drag (move by delta, with gain) and keep the controlled object away from the screen edge.
- Make interactive objects chunky (>= 10 px thick) and give them volume (bevel/highlight), not 1-6 px lines.
- Text glyphs: a 3x5 font at scale 2 reads as "squares". Effects that must be recognisable (Z of sleep) need dedicated pixel art at >= 10 px.

## Character animation
- A state must be unambiguous at a glance: "sleep" needs props (bed, pillow, blanket), lying pose, and explicit phases (eyes close slowly, open slowly, get up, stretch big). Subtle body offsets read as "sleeping while standing".
- Showpieces need lead-in/lead-out: every act starts/ends in the base pose at the current position; walk to a showpiece's start instead of cutting.

- Interaction with an object must physically reach it: watering = the can's spout above the plant (stretch up on the legs), not at the cabinet's height.
- Props at 1:1 next to a 107 px mascot need >= 20-30 px to read; keep them off the eyes (move hands in front of the body instead).
- New content must show up often enough to be noticed: tune weights so a new act appears within ~1 minute (then the user can dial it down).
- "Less of X, more of the others" means spread evenly, not crown a new favourite (gym x3 read as "always in the gym"). Measure the act mix over an hour on the host (session showlong) before and after, and keep the visible acts within ~2x of each other.
- Never repeat the same prop act back to back, also across short ambient acts in between.

- A three-quarter view needs real geometry: project a few boxes (body, cabin, wheels) with yaw + a camera above. Hand-placed 2D pixel rows read as a flat front ("lights front and back").
- Draw independent object kinds in their own pass. Rivals drawn inside the obstacle loop vanished whenever no obstacles were left (at the finish).
- Never move or restyle an approved UI element to satisfy a new request unless asked. "Rivals arrive after the banner" meant behind the banner, not "move the banner"; the user had to ask for it back.
- A drift is yaw, not roll: show it with a three-quarter sprite sliding sideways. Rotating a rear-view sprite in the image plane reads as "driving on two wheels".
- Damage effects must not cover what the player steers by: hit cracks go to the edges; the full centre shatter only on game over.
- Start games easier than feels right on a desktop preview: the small screen and tilt control halve the player's reaction budget (race speed had to drop ~55%).

## Display colors
- This IPS panel washes colors toward white. Calibrate on the device with side-by-side variants and let the user pick (chosen: gamma 1.4, saturation x1.1).
- Dark art on a black background disappears: lift near-black props (not eyes) to mid-gray, lighten dark blues.

## Sensors
- Calibrate motion effects on real logs first (here: shake jolts 2-5 g, handling ~0.5 g, rest 0.01 g), then make the reaction big: "earthquake" means across the whole screen, not a few-pixel jiggle.
- Drive effects from the full 3-axis jolt; users shake along any axis (towards/away = z barely moved the mascot).
- Never guess IMU axes: calibrate with 3 held poses (upright, tilted right-down, flat) and check the right-handed triple. Here: +X = screen up, +Y = screen right, -Z = out of the screen. Start the log *before* asking for the poses.
- Reset sensor/gesture state in app init: statics leaking between host test sessions hid a working feature.

## Performance (ESP32-C6: no FPU)
- Every float op is software: ~25 float ops in gfx_color cost ~12 us, and hundreds of calls per frame ate 8 ms. Keep per-pixel/per-row hot paths integer (tables, fixed point), compute colors once, and never call sqrtf/expf per row in a loop that runs every frame.
- Measure new scenery on the board before calling it done: the host preview hides it. A temporary scripted-touch build that logs per-section microseconds found the cost in two flashes; strip it afterwards.

## Process
- Never pass `-DCMAKE_C_FLAGS=...` to idf.py: it replaces the toolchain's base flags (`-march=rv32imac...`). Use a component `target_compile_options` or `idf.py fullclean` to recover.
- Auto mode blocks executing third-party bundles; parse data statically and port logic by hand instead.

## Препятствия должны оставлять видимый объезд
- Объекты рисуются крупнее хитбокса (DRAW_SCALE). Если поставить два таких объекта рядом, они визуально закрывают всю дорогу, даже когда по хитбоксу проезд есть.
- Для составных препятствий (авария из двух машин) проверять по раскадровке, что видимая свободная полоса есть всегда. Второй объект ставить к обочине, а не к центру.

## Не фиксировать зерно в генераторах игрового контента
- `rng_seed(константа)` перед генерацией уровня делает каждую игру одинаковой. Это скучно, и пользователь это замечает.
- Зерно берётся из общего потока RNG и подмешивается время действия игрока (тапа). Детерминизм нужен только тестам, и он уже есть: на Mac фиксированное зерно сессии и фиксированное время.
- После перехода на случайный контент кадры раскадровок, подобранные под старые позиции (авария, препятствия), надо подбирать заново.

## Механика и корпуса: чертежи вместо планов
- Если пользователь прислал эскиз компоновки, это и есть задание. Не допрашивать его о подписях и цифрах на эскизе: они могут ничего не значить. Размеры брать из официального чертежа и прямых замеров пользователя.
- Не писать длинный план для корпуса. Сразу делать модель и показывать чертежи (виды, разрезы, размеры) и превью по шагам: лоток → рамка → крышка → сборка. Правки вносить итеративно.
- Неизвестные размеры (пружины, толщина пластин, рычажок выключателя) выносить в параметры с разумными значениями, а не задавать вопросы до начала работы.
- Не перекладывать на пользователя проверку, которую можно сделать по имеющимся данным: чертёж размеров (вид сзади) и STEP платы уже были в сессии. Сначала свериться с ними, пользователю оставлять только то, чего в данных нет (реальные замеры, свои провода).
