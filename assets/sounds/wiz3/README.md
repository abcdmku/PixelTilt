# Wiz3 original sounds

These `.au` files are the original μ-law clips shipped inside the creator's
Wiz3 Java runtime JAR. They are kept alongside the generated PTA PCM8 sample
bank in `games/wiz3/assets.h` used by the browser and ESP32 ports;
`silence.au`, `tap.au`, and `walk.au` are included even where the port only
uses them in contextual effects.

Source: https://www.eaborn.com/wiz3/wiz3web.html

The reproducible extraction command is:

```text
python tools/extract-wiz3-assets.py --jar wiz3-original.jar
```
