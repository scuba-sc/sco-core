# sco-core docs site design contract

The site extends Astro Starlight 0.42.6 with a restrained, task-first documentation
design. Charcoal dark mode and warm-neutral light mode share one teal accent.
Documentation layout, typography and color values live in `src/styles/portal.css`.
The home-only visual layer lives in `src/styles/landing.css`. Extend this contract
before adding a new visual primitive.

## 1. Source of truth

- The supported Starlight `Hero` override renders `LandingHero.astro` only for
  the root `index` entry (normalized to an empty route ID by Starlight). Other
  hero routes, including 404, use Starlight's default
  Hero. The generated home uses `template: splash`; its full documentation body
  remains below the landing section.
- Starlight owns navigation, theme selection, accessibility landmarks and
  Expressive Code blocks with their copy buttons. The local Search override retains
  its native dialog and lazy Pagefind UI. Custom CSS uses Starlight public tokens.
- The redesign applies Open Design's redesign, typography, color, anti-AI-slop and
  state-coverage guidance within the constraints of a static documentation site.
- No web fonts, remote assets or new dependencies. System fonts render immediately.
  Search adds a small local query normalizer and category controls around Pagefind;
  its UI and index remain lazily loaded from the static site. The existing inline
  filter and deep-link script on `/natives/` makes no network requests.
- `scripts/gen.py` generates content and the semantic markup classes below. Stable
  routes, source metadata, release status and evidence qualifications remain intact.

## 2. Color

Documentation palette values are centralized on `:root` and its light-theme variant. The
`--sco-*` palette maps into Starlight's existing tokens so the shell and page content
use the same surfaces. Semantic Starlight status ramps retain their separate purpose.

| Role | Dark | Light | Token |
| --- | --- | --- | --- |
| Page and navigation | `#181b1b` | `#faf9f6` | `--sco-bg` |
| Raised surface | `#212525` | `#f0f0eb` | `--sco-surface` |
| Heading / strong text | `#eef1ed` | `#202723` | `--sco-heading` |
| Body text | `#cdd2ce` | `#39463f` | `--sco-text` |
| Secondary text | `#a1aaa5` | `#5f6d64` | `--sco-muted` |
| Structural rule | `#353b38` | `#d5dcd5` | `--sco-rule` |
| Form control outline | `#738079` | `#7d8b81` | `--sco-control-border` |
| Teal action / focus | `#70cfba` | `#166653` | `--sco-accent` |
| Text on filled action | `#102f29` | `#f5fbf7` | `--sco-on-accent` |
| Current / target surface | `#233e36` | `#e2f1eb` | `--sco-target` |

Keep accent use concentrated on the primary action, current navigation and keyboard
focus. Ordinary content links are neutral and underlined; their hover state uses
teal. Hub numbers are muted navigation aids, not decoration.

Published and tier 1 use Starlight's green ramps; preview and tier 3 use orange;
tier 4 uses red; tier 2 is neutral. Every chip also names its status. Structural
rules are subtle; control boundaries and focus indicators need stronger contrast.

The home landing retains the supplied deepwater identity in both themes:
`#030d16` background, `#8de4e0` teal, white display text, `#dae7ea` supporting
copy, and translucent teal rules. These values are scoped to `[data-sco-landing]`;
the documentation body and built-in header keep the selected Starlight theme.
The right-hand particle field sits behind a dark veil so text remains readable
when particles pass underneath it. The CSS glow remains with JavaScript off or
field initialization unavailable.

## 3. Typography

Use Starlight's local system sans and monospace stacks, with no font download.
Body copy is 1rem with 1.6 line height. Read / emphasize / announce weights are
400 / 500 / 600. Page titles use a responsive 2rem–2.625rem scale, 1.15 line height
and -0.02em tracking. Section headings use the 1.5rem Starlight size. Card titles
use 1.125rem; facts and sidebar links 0.875rem; labels and chips 0.8125rem.

Paragraphs are limited to 68ch within a 46rem reading column. Titles balance their
lines. Small labels use modest positive tracking; the intro eyebrow uses sentence
case. Monospace is reserved for identifiers, versions, archives, hashes and code.
Facts and counts use tabular figures. Long prose identifiers wrap without truncation.

The landing title is a single semantic `h1#_top[data-page-title]`: **sco-core SDK**,
with a responsive 3rem–8rem system-sans scale, tight line height and tracking. Its
tagline remains **Public contracts for Star Citizen offline plugins**. Monospace
eyebrow, language links, hub indices and footer details echo the supplied design;
no rendered image contains text.

## 4. Spacing and layout

The spacing scale is 0.25, 0.5, 0.75, 1, 1.5 and 2rem (`--sco-space-1` through
`--sco-space-6`). Chip radius is 0.25rem; panel/action radius is 0.375rem. Hubs use
open rows with a top rule and no radius, colored rail, shadow or decorative icon.

The desktop sidebar is 16rem. Starlight retains its native mobile menu, table of
contents breakpoints and navigation behavior. Sidebar groups start collapsed,
except groups that contain the current page; Starlight retains user expansion state.
Current links use a tinted surface, stronger weight and underline. Hover and focus
do not move elements or change layout.

The home landing is full width within Starlight's content panel. Its bracket/core
mark, large title, tagline, **Start a plugin**, **Browse API**, four language links
and four thinly ruled library links use normal responsive flow. The content is
never a fixed 1920px composition or zoomed canvas. The original evidence banner,
library summaries and release metadata continue below in a centered reading area.
Actions wrap naturally, remain at least 2.75rem high and do not depend on a wide
viewport. The native header and search retain their semantics. On the splash
page, the native theme selector is also visible in the mobile header.

## 5. Primitives

- **Landing** (`[data-sco-landing]`, `.sco-landing__*`): home-only semantic
  section, one page title, linked tasks/languages/hubs, decorative bracket/core
  mark, canvas and simple motion toggle. The canvas is `aria-hidden`; all content
  and navigation exist in server-rendered HTML.
- **Intro** (`.sco-intro`, `.sco-intro__eyebrow`, `.sco-intro__lead`): compact,
  left-aligned explanation following the page title.
- **Actions** (`.sco-actions`, `.sco-action`, `.sco-action--primary`): real links;
  one filled primary action and a quiet underlined secondary action.
- **Language shortcuts** (`.sco-language-links`): wrapping, underlined text links.
- **Catalogue snapshot notice**: the existing labelled caution aside, set at
  0.875rem with a restrained semantic tint and 2px caution rule. Its full source SHA,
  release qualification and verification warning remain visible.
- **Hub card** (`.sco-card--hub` in `.sco-hubs`): muted index, title link, summary
  and metadata, separated with open rules in canonical sidebar order.
- **Track panel** (`.sco-track`): raised surface, status chip, title, fact list and note.
- **Profile card** (`.sco-profile`): heading, archive, version, purpose, relation and
  guide/verification links. **Link card** (`.sco-card`) uses the same quiet surface.
- **Fact list** (`.sco-facts`): semantic `<dl>`, muted labels and wrapping monospace values.
- **Status / tier chips** (`.sco-status--published`, `.sco-status--preview`,
  `.sco-tier--1` through `--4`): semantic color and explicit text labels.
- **Version pill** (`.sco-pill`): neutral bordered monospace label.
- **Link list** (`.sco-links`): column-responsive grid of service links.
- **Tier legend** (`.sco-tiers`): chip, meaning and row count in a `<dl>`.
- **Filter bar** (`.sco-natives__filters`, `.sco-field`): visibly labelled search
  and selects, hidden until the existing script runs.
- **Search dialog** (`site-search`, `#starlight__search`): task hint, visible loading
  and retry states, search input, Type filters, results and an **All categories**
  reset. A labelled native Type select offers APIs, Guides, Languages, Services
  and Overview, with All categories as its default. The select and reset wrap
  above results in narrow panes. One category is active at a time. The query stays editable, and any normalization is explained in text.
- **Native table** (`.sco-natives__table`): row id, subsystem/source, hash, tier
  and capabilities. Code fences keep Expressive Code's copy button.

## 6. Motion and performance

HyperFrames informs responsive feedback and stillness after an action; this is
interface CSS, with no video, player, GSAP or animation dependency. New motion is
opt-in under `prefers-reduced-motion: no-preference`: action, sidebar and search
control colors use 120ms ease-out transitions. A desktop search dialog may enter
over 160ms from 0.96 opacity and a 4px vertical offset. No exit delay, result
animation, layout animation, smooth scrolling or prose reveal on documentation routes. Documentation
content and search results have no looping motion.
Focus outlines and active feedback remain immediate. Reduced motion disables
these effects. Disclosure markers
remain still. Cards are not click targets; their labelled links are.

The landing adds a local canvas particle field through `initParticleField(root)`.
The enlarged galaxy extends beyond the hero edges, clipped by its isolated
section. A sparse foreground canvas lets a few stars and thin, short trails pass
across the hero text; most particles stay behind the text veil. Both layers use
the same simulation snapshot, with no extra worker or blur filter. Canvas layers
ignore pointer events and cannot cover the documentation below the hero.
Its bundled worker samples a deterministic ten-second orbital loop; counts are
capped at 12,000 desktop and 3,000 mobile particles, with bounded DPR, at most
one request in flight and a maximum 30fps update cadence. These are implementation
limits, not a measured speed claim. The toggle is hidden until the first frame is
ready. **Pause animation** freezes the phase; **Resume animation** continues it.
Reduced motion keeps a static frame and hides the toggle. Paused, hidden and
offscreen fields stop recurring work. Worker failure retains the static CSS
fallback and usable links. Runtime teardown handles pagehide and restoration
from the back/forward cache; the component also disposes before an Astro swap.
There is no telemetry panel, GSAP, video, external font or animation dependency.

On the home page, native CSS scroll timelines recede and fade the hero copy and
move the two particle planes at different depths. Home body blocks fade at the
viewport edges and remain opaque through the central reading area. Scrolling
back reverses these effects. Focused or targeted sections stay fully visible.
The effects add no scroll listener or animation loop. Unsupported browsers and
reduced motion retain visible, still content; documentation routes stay still.

The orbital model caches per-particle rotations and uses global harmonics per
frame. Its approximation must keep deterministic ten-second closure and maximum
position error below 0.01 reference pixels in a same-input comparison. This
reduces repeated trigonometry at the cost of bounded parameter storage; a math
microbenchmark does not establish whole-page rendering speed.

The site remains statically generated. Search normalization uses a bounded task
vocabulary, preserves exact symbols and quoted searches, and leaves unknown terms
intact. Visible weighted task phrases point to canonical contracts, not new API
aliases or support claims. Pagefind owns matching and category filtering; the
custom code must retain lazy loading, local requests, keyboard operation, and
useful loading, retry and empty states. Compare shipped JS/CSS sizes with the
accepted baseline when changing this search boundary.

## 7. Responsive behavior

Card grids use `auto-fit` with a minimum column size capped at 100% of the available
space. A narrow content pane stays one column even when the surrounding window is
wide. `.sco-cards--single` always remains one column. Long SHAs, archives, URLs and
inline code wrap with `overflow-wrap: anywhere` rather than truncating.

`/natives/` has no table of contents and uses a 60rem content column. Its existing
container query turns table rows into labelled blocks below 40rem. At very narrow
column widths, those blocks stack labels above values. Other Markdown tables keep
Starlight's internal horizontal scroll; the document itself must not overflow.

The landing uses four hub columns on wide screens, two on intermediate widths
and one on small phones. Its title, actions, language links, footer and native
header controls wrap without clipping. Check home at 360, 390, 430, 600, 820,
1024, 1366, 1440 and 1920px in both themes, including JavaScript-off, reduced
motion, paused and unavailable field states.

Check guide, API/table, service, language, long identifiers and search
filters at 360, 768, 1280 and 1600px in both themes. Search hints, category names
and reset controls must wrap without clipping; results remain full width. Retain
the native index's populated, no-results and unknown-hash states.
Its data is already server-rendered, so no new loading state is introduced.

## 8. Accessibility constraints and accepted debt

- Keep real headings, links, lists, `<dl>` facts and labelled sections. Preserve
  the skip link, native menu focus management, search and theme controls.
- Keyboard focus uses a 2px teal outline with a 3px offset on links, fields,
  buttons and summaries. Current navigation also uses weight and underline.
- Search preserves Ctrl/Cmd-K, Tab/Enter, Escape, dialog focus containment and
  focus restoration, including a first opening before Pagefind finishes loading.
  Category filters retain labelled native controls. Selecting a category replaces
  the previous selection; deselecting or resetting searches all categories.
- Small text requires 4.5:1 contrast; focus and control boundaries require 3:1.
  Check both themes; status chips use Starlight's high-on-low semantic ramps.
- The native index keeps its visible labels and `role="status"` row count.
  Deep links retain `aria-current="location"` and focus (`tabindex="-1"`);
  unknown hashes retain the `role="alert"` notice. Without JavaScript the full
  table is visible and indexed by Pagefind.
- Accepted debt: generated pages remain Markdown with semantic HTML primitives,
  because catalogue prose is not MDX-safe. No component-framework migration is needed.
