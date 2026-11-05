# RiseVerb — by M1lobeats

Maakt van een vocal een **reverse-reverb riser**: een swell die opbouwt en precies eindigt waar je vocal begint. Werkt als Audio Unit in Logic Pro (en als VST3).

## Gebruik in Logic Pro

1. Zet **RiseVerb** als Audio FX op je vocaltrack (Audio Units → M1lobeats → RiseVerb).
2. Klik **CAPTURE** en speel het stuk vocal af. Stop je de playback in Logic, dan stopt de capture vanzelf (of klik **STOP**).
   - Je kan ook een audiobestand op de plugin slepen of **LOAD FILE** gebruiken.
3. Kies een **preset** bovenaan of stel zelf in. Elke wijziging rendert meteen opnieuw.
4. **PREVIEW** speelt de riser met daarna het begin van je vocal. Staat Logic stil, dan speelt RiseVerb de preview zelf af via je standaard audio-uitgang.
5. Sleep **DRAG TO DAW** naar een lege track en zet het einde van de riser op de start van je vocal. Klik je zonder te slepen, dan komt het bestand in `Muziek/RiseVerb`.

### Knoppen
- **LENGTH**: 1/2, 1, 2 of 4 maten, op het tempo van Logic
- **SIZE / TONE / WIDTH**: lengte van de staart, donker ↔ helder, mono ↔ breed
- **STUTTER**: *Smooth*, *Gate* (harde chops) of *Repeat* (stu-stu-stutter). **RATE**: 1/4 tot 1/32 of *Build ↑*. **DEPTH**: hoeveel (0 % = uit)
- **OUTPUT**: volume

### One-click FX
| Knop | Wat het doet |
|---|---|
| SHIMMER | octaaf hoger erbij, luchtig en glanzend |
| DARK OCT | octaaf lager erbij, donker en zwaar |
| PITCH RISE | toonhoogte klimt een octaaf omhoog richting de vocal |
| FILTER SWEEP | resonant low-pass filter dat opengaat richting de vocal |
| LO-FI | bitcrush + downsample |
| RADIO | telefoon/radio-klank met drive |
| DELAY | tempo-gesyncte ping-pong delay (1/8 dotted) |
| TAPE | wow & flutter + warme saturatie |
| DE-ESS AUTO | detecteert scherpe S-klanken automatisch en houdt ze uit de reverb (standaard aan) |

### Presets
13 factory presets (Classic Swell, Ghost Riser, Shimmer Lift, Stutter Build, Repeat Hype, Trap Chop, Pitch Climb, Lo-Fi Tape, Radio Rise, Dark Octave, Echo Rise, Big Drop Build…).
Met **SAVE** bewaar je je eigen instellingen als user preset in `Muziek/RiseVerb/Presets`.

## Installeren (Mac)

1. Download `RiseVerb-mac.zip` bij **Releases** (of bij Actions → laatste build → Artifacts) en pak het uit.
2. Rechtsklik op `Install-RiseVerb.command` → **Open** → **Open**. (Rechtsklik is nodig omdat de plugin niet door Apple gesigneerd is.)
3. Herstart Logic Pro.

Lukt het script niet? Doe het dan manueel in Terminal:

```bash
cp -R RiseVerb.component ~/Library/Audio/Plug-Ins/Components/
cp -R RiseVerb.vst3 ~/Library/Audio/Plug-Ins/VST3/
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/RiseVerb.component ~/Library/Audio/Plug-Ins/VST3/RiseVerb.vst3
killall -9 AudioComponentRegistrar
```

Verschijnt de plugin niet in Logic: Logic Pro → Settings → Plug-in Manager → RiseVerb → **Reset & Rescan Selection**.

## Bouwen

GitHub bouwt automatisch bij elke push naar `main` (zie `.github/workflows/build-mac.yml`). Daarbij wordt de AU ook met Apple's `auval` gecontroleerd.

Zelf bouwen op een Mac (Xcode + CMake nodig):

```bash
git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git JUCE
cmake -B build -G Xcode
cmake --build build --config Release --target RiseVerb_AU RiseVerb_VST3
```

## Hoe het werkt

`riser = reverse( reverb( reverse(vocal) ) )`. Alleen het deel vóór de vocal wordt behouden. De reverb is een convolutie met een gegenereerde, decorreleerde stereo-impulsrespons, waarvan de lengte meeschaalt met de gekozen maatlengte. Daarna komen high-pass (140 Hz), M/S-width, tempo-gesyncte stutter (smooth/gate/repeat), fade-in, zachte saturatie en normalisatie naar -1 dBFS. Zie `Source/RiserEngine.h`.

Gebouwd met JUCE 8 (gratis voor persoonlijk gebruik; check de JUCE-licentie als je de plugin wil verkopen).
