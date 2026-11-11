#!/usr/bin/env python3
"""WCAG 2.2 contrast of the Theme colour roles, read from Theme.qml. MIT.

Every text role must reach 4.5:1 on each surface it is drawn on (1.4.3), and
every outline, focus ring and status glyph 3:1 (1.4.11). A changed role that
falls short fails here, with the pair named. Disabled text is exempt (WCAG
exempts inactive components). Standard library only; no Qt needed.
"""
import pathlib
import re
import sys

THEME = pathlib.Path(__file__).resolve().parents[2] / 'lib/qml/Telemetry/UX/Theme.qml'

TEXT = 4.5
GRAPHIC = 3.0

# (foreground role, background roles, minimum ratio, why)
PAIRS = [
    ('textPrimary', ['surfaceWindow', 'surface', 'surfaceBar', 'surfaceAlternate', 'stateHover', 'statePressed',
                     'stateSelected', 'buttonFill', 'buttonFillPressed', 'neutralContainer'], TEXT, 'body text'),
    ('textSecondary', ['surfaceWindow', 'surface', 'surfaceBar', 'surfaceAlternate', 'stateHover', 'statePressed',
                       'buttonFill', 'neutralContainer'], TEXT, 'secondary text, links at rest, hovered, pressed'),
    ('textMuted', ['surfaceWindow', 'surface', 'surfaceBar', 'surfaceAlternate', 'stateHover', 'statePressed',
                   'neutralContainer'], TEXT, 'section labels, times, descriptions, placeholders'),
    ('selectedForeground', ['stateSelected'], TEXT, 'text on a selected row'),
    ('accentForeground', ['accent', 'accentPressed'], TEXT, 'the primary action'),
    ('success', ['successContainer', 'surfaceWindow'], TEXT, 'Connected'),
    ('warning', ['warningContainer', 'surfaceWindow', 'surface'], TEXT, 'Interrupted, changes allowed'),
    ('error', ['errorContainer', 'surfaceWindow', 'surface', 'surfaceBar'], TEXT, 'failures, the error count'),
    ('errorLink', ['errorContainer', 'stateHover', 'statePressed'], TEXT, 'Retry in a failure chip'),
    ('info', ['infoContainer', 'surfaceWindow', 'stateHover'], TEXT, 'polling, connecting'),
    ('infoLink', ['infoContainer', 'stateHover', 'statePressed'], TEXT, 'Stop in the polling chip'),
    ('success', ['surface', 'surfaceAlternate'], GRAPHIC, 'the Ok dot'),
    ('warning', ['surfaceAlternate'], GRAPHIC, 'a warning glyph'),
    ('outline', ['surfaceWindow', 'surface', 'surfaceBar'], GRAPHIC, 'field and selector borders'),
    ('focusRing', ['surfaceWindow', 'surface', 'surfaceBar', 'stateHover', 'stateSelected'], GRAPHIC,
     'focus frames'),
    ('accent', ['surfaceWindow', 'surfaceBar'], GRAPHIC, 'the primary button against its surroundings'),
]


# Roles that are never measured, each with the reason. Every role of the Theme
# must be in a checked pair or here: a new role cannot slip past unmeasured.
EXEMPT = {
    'textDisabled': 'text of inactive controls; WCAG 1.4.3 exempts inactive components',
    'surfaceDisabled': 'fill of inactive controls; exempt likewise',
    'outlineVariant': 'separators and disabled borders carry no information (1.4.11)',
}


def roles(text: str) -> dict:
    found = dict(re.findall(r'property color (\w+):\s*"(#[0-9a-fA-F]{6})"', text))
    if not found:
        sys.exit(f'No colour roles found in {THEME}')
    return found


def luminance(hex_colour: str) -> float:
    def channel(value: int) -> float:
        c = value / 255
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = (int(hex_colour[i:i + 2], 16) for i in (1, 3, 5))
    return 0.2126 * channel(r) + 0.7152 * channel(g) + 0.0722 * channel(b)


def contrast(a: str, b: str) -> float:
    la, lb = sorted((luminance(a), luminance(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


def main() -> int:
    # Negative control: the muted text this replaced (#718297 on the popover
    # surface, 4.18:1) must fail, or the check proves nothing.
    if contrast('#718297', '#17202b') >= TEXT:
        print('FAIL the negative control passed: the contrast formula is wrong')
        return 1
    colours = roles(THEME.read_text(encoding='utf-8'))
    covered = {fg for fg, _, _, _ in PAIRS} | {bg for _, bgs, _, _ in PAIRS for bg in bgs} | set(EXEMPT)
    uncovered = sorted(set(colours) - covered)
    if uncovered:
        print('FAIL roles neither measured nor exempt: ' + ', '.join(uncovered))
        return 1
    failures = 0
    checked = 0
    for foreground, backgrounds, minimum, why in PAIRS:
        for background in backgrounds:
            if foreground not in colours or background not in colours:
                print(f'FAIL missing role: {foreground if foreground not in colours else background}')
                failures += 1
                continue
            ratio = contrast(colours[foreground], colours[background])
            checked += 1
            ok = ratio >= minimum
            failures += not ok
            print(f"{'ok  ' if ok else 'FAIL'} {ratio:5.2f}:1 (>= {minimum}) {foreground} on {background} - {why}")
    print(f'{checked - failures} of {checked} pairs meet WCAG 2.2' if not failures
          else f'{failures} pair(s) below their minimum')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
