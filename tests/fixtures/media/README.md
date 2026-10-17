# Test media

Generated, no third-party content.

- `clip.mp4`: 4 s, 320x180, 25 fps H.264 colour pattern (FFmpeg `testsrc2`) with a 440 Hz AAC tone:

      ffmpeg -f lavfi -i testsrc2=size=320x180:rate=25 -f lavfi -i sine=frequency=440:sample_rate=48000 -t 4 \
          -c:v libx264 -pix_fmt yuv420p -g 25 -c:a aac -b:a 64k -movflags +faststart clip.mp4

- `one.vtt`: one WebVTT cue ("Bonjour", 0-3 s).
- `tone.mp3`: 1.5 s stereo 440 Hz tone, the music tests' track:

      ffmpeg -f lavfi -i sine=frequency=440:duration=1.5:sample_rate=44100 -ac 2 -c:a libmp3lame -b:a 64k \
          -metadata title=tone tone.mp3
