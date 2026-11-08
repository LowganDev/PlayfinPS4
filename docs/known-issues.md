# Known limitations

## Behaviour

- Quick Connect is covered by the client's unit tests, not by the end-to-end screen tests (it polls every 5 s).
- A server whose key changed shows a TLS error, not a dedicated dialog with both fingerprints.
- If the PS4 keyboard cannot open (no signed-in user), the built-in editor needs a physical keyboard.
- Subtitles are drawn at the bottom of the picture, under the progress bar while it shows (it hides after 4 s).
- The stop report may be lost when the app is closed from the PS4 menu (the system ends the process).
- Chinese subtitles use the Japanese forms of the Han characters (the fallback takes the first CJK face).
- "En direct" shows the programme on now, not the next one; with very large channel lists (1000+) it builds one row
  per channel.
- Live TV shows no message when the server has no channels.
- Switching seasons very quickly may show the season asked first; a favourite pressed twice very quickly is sent
  twice with the same value.
- A Resume or Next up row that changes after playback is updated at the next visit of the home screen.
- The Linux `--renderer gl` mode does not drive the player.
- 2.0: the music has no visualizer: it needs FFmpeg's audio filters, left out of the PS4 dependency package
  (rebuilding and publishing it is a release step).
- 2.0: search results come after the keyboard closes (the PS4 system keyboard is a dialog: nothing can be shown
  while typing).
- 2.0: PS4 accounts each start on their own user, but the users saved on the console (and their sign-ins) are
  common to all accounts: a saved user can be chosen from any account without a password.
- 2.0: a download is the original file, whatever its format; one that the console cannot decode plays badly
  offline (no server to transcode). A download played offline tells the server nothing (watched state, resume).
- 2.0: Live TV channels always go through the server (no direct M3U playback, no guide downloaded on the
  console).

## Performance (PS4)

- While the focus moves, a frame takes about 14-19 ms on home and in a library grid (60 fps at rest: nothing is
  redrawn); most of the time is spent copying posters.
- Starting and stopping a video shows 4-5 frames of about 95 ms (the spinner stutters) before the first picture.

## Possible improvements to the code

- Split the rasterizer's `fillShape` and turn its counters into an array indexed by an enum.
- Share the GL and PS4 presenters; one helper for modal cards; `Translator::plural` on top of `format`.
- A paged-grid helper for the video and music libraries; one JSON writer.
- Split `PlayerScreen` (overlay, extras) and the error mapping of `HttpClient::send`.
- Cache `Label::measure` by width.
