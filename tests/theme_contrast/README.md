# Theme contrast

`python3 -B tests/theme_contrast/run.py` reads the colour roles from
[Theme.qml](../../lib/qml/Telemetry/UX/Theme.qml) and computes the WCAG 2.2 contrast
of every pair the panels draw:

- text on each surface it appears on: 4.5:1 or more (1.4.3);
- outlines, focus rings and the primary button: 3:1 or more (1.4.11).

Disabled text is exempt, as WCAG exempts inactive components. A pair that falls short
fails and is named.

**Negative control:** the muted text the Theme replaced (`#718297` on `#17202b`,
4.18:1) must fail, so that a broken formula cannot pass silently.

**Running it:** it uses the standard library only and needs no Qt. CI runs it in the
Linux panel job.

**A new pair:** when a panel starts drawing a role on a surface not listed in `PAIRS`,
add the pair there.
