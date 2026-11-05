# RiseVerb

Maakt van een vocal een **reverse-reverb riser**: een swell die opbouwt en precies eindigt waar je vocal begint. Werkt als Audio Unit in Logic Pro (en als VST3).

## Gebruik in Logic Pro

1. Zet **RiseVerb** als Audio FX op je vocaltrack (Audio Units → Michiel → RiseVerb).
2. Klik **CAPTURE** en speel het stuk vocal af (bv. de eerste woorden van je hook). Klik **STOP**.
   - Of sleep een audiobestand op de plugin, of gebruik **LOAD FILE**.
3. Kies de **LENGTH** (1/2, 1, 2 of 4 bars). Het tempo neemt RiseVerb automatisch over van Logic.
4. Stel het geluid bij. Elke wijziging rendert de riser meteen opnieuw:
   - **SIZE**: hoe lang en vol de reverb-staart is
   - **TONE**: donker ↔ helder
   - **WIDTH**: mono ↔ breed stereo
   - **TREMOLO + DEPTH**: ritmische chop (1/4 tot 1/32) op het tempo van je track
   - **OUTPUT**: volume van de riser
5. **PREVIEW** speelt de riser met daarna het begin van je vocal, zodat je de overgang hoort.
6. Sleep **DRAG TO DAW** naar een lege track in Logic. Zet het einde van de riser op het moment dat je vocal start.
   Klik je zonder te slepen, dan wordt het bestand bewaard in `Muziek/RiseVerb` en geopend in Finder.

De riser is altijd exact het gekozen aantal maten lang, zodat hij op het grid snapt.

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

`riser = reverse( reverb( reverse(vocal) ) )`. Alleen het deel vóór de vocal wordt behouden. De reverb is een convolutie met een gegenereerde, decorreleerde stereo-impulsrespons, waarvan de lengte meeschaalt met de gekozen maatlengte. Daarna komen high-pass (140 Hz), M/S-width, tempo-gesyncte tremolo, fade-in, zachte saturatie en normalisatie naar -1 dBFS. Zie `Source/RiserEngine.h`.

Gebouwd met JUCE 8 (gratis voor persoonlijk gebruik; check de JUCE-licentie als je de plugin wil verkopen).
