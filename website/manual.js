// VibeRa manual: search, contents spy, workspace tour, stepper, tabs, effects, shortcuts and hacks.
(() => {
  const reduced = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  const $ = (s, root = document) => root.querySelector(s);
  const $$ = (s, root = document) => [...root.querySelectorAll(s)];
  const esc = s => s.replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  const keys = combo => combo.split(' + ').map(k => `<kbd>${esc(k)}</kbd>`).join('+');

  // ---------- Reading progress, reveal and contents spy ----------
  const progress = $('#progress');
  addEventListener('scroll', () => {
    const max = document.documentElement.scrollHeight - innerHeight;
    progress.style.width = `${max > 0 ? (scrollY / max) * 100 : 0}%`;
  }, { passive: true });

  const sections = $$('.m-sec');
  const tocLinks = new Map($$('.toc a').map(a => [a.getAttribute('href').slice(1), a]));
  if ('IntersectionObserver' in window) {
    if (!reduced) {
      sections.forEach(s => s.classList.add('reveal'));
      const reveal = new IntersectionObserver(entries => entries.forEach(e => {
        if (e.isIntersecting) { e.target.classList.add('in'); reveal.unobserve(e.target); }
      }), { rootMargin: '0px 0px -8% 0px' });
      sections.forEach(s => reveal.observe(s));
    }
    const spy = new IntersectionObserver(entries => entries.forEach(e => {
      if (!e.isIntersecting) return;
      tocLinks.forEach(a => a.classList.remove('active'));
      tocLinks.get(e.target.id)?.classList.add('active');
    }), { rootMargin: '-30% 0px -65% 0px' });
    sections.forEach(s => spy.observe(s));
  }

  // Count-up facts.
  $$('[data-count]').forEach(el => {
    const target = +el.dataset.count;
    if (reduced) return;
    let n = 0;
    const tick = () => { n = Math.min(target, n + Math.max(1, Math.round(target / 30))); el.textContent = n; if (n < target) requestAnimationFrame(tick); };
    el.textContent = '0';
    new IntersectionObserver((e, o) => { if (e[0].isIntersecting) { tick(); o.disconnect(); } }).observe(el);
  });

  // ---------- Lightbox ----------
  const box = $('#lightbox');
  $$('.shot img, .tour img').forEach(img => img.addEventListener('click', () => {
    box.querySelector('img').src = img.src;
    box.querySelector('img').alt = img.alt;
    box.hidden = false;
  }));
  box.addEventListener('click', () => { box.hidden = true; });
  addEventListener('keydown', e => { if (e.key === 'Escape') box.hidden = true; });

  // ---------- Tabs ----------
  $$('[data-tabs]').forEach(group => {
    const buttons = $$('.tab-bar button', group), panes = $$('.tab', group);
    buttons.forEach((b, i) => b.addEventListener('click', () => {
      buttons.forEach((x, j) => x.classList.toggle('on', i === j));
      panes.forEach((x, j) => x.classList.toggle('on', i === j));
    }));
  });

  // ---------- Workspace tour ----------
  const stops = [
    ['Project bar', 'Open, save and export, Undo and Redo, the Demo song, Settings, metronome, count-in, the typing keyboard, Tools and the CoProducer panel.', [0.6, 4.2, 39.8, 3]],
    ['Transport', 'Play / pause with Space, stop, record with R and the audio input monitor.', [0.75, 8.1, 12.1, 3.6]],
    ['Pattern or Song', 'Pattern mode loops the selected Pattern; Song mode plays the Playlist. Press L to switch. The loop button repeats the selection.', [13.1, 8.1, 6.9, 3.6]],
    ['Tempo', 'Type a tempo, nudge it with - and +, or tap it in with Tap.', [20.3, 8.1, 10, 3.6]],
    ['Pattern selector', 'Choose which Pattern the Channel Rack and Piano roll edit. + makes a new Pattern; F4 jumps to the next empty one.', [30.6, 8.1, 16.9, 3.6]],
    ['Snap', 'The global grid for editing: bars, beats, steps, triplets and more.', [48, 8.1, 5.9, 3.6]],
    ['Song position', 'Bar and beat of the playhead and the current time signature.', [54.75, 8.1, 12.1, 3.6]],
    ['Editor buttons', 'Show or hide the Browser, Channel Rack, Piano roll, Playlist and Mixer.', [73.25, 8.1, 25.6, 3.6]],
    ['Browser', 'Your samples, instruments, recent files and the current project. Drag anything into the Rack, Playlist or Mixer.', [0.3, 13, 14.3, 83.8]],
    ['Channel Rack (F6)', 'Every sound in the project, with a step sequencer for the selected Pattern.', [15, 13, 28.1, 23.2]],
    ['Piano roll (F7)', 'Melodies, chords and every note detail for the selected Channel.', [15, 37.2, 28.1, 59.6]],
    ['Playlist (F5)', 'The song timeline: Pattern, audio and automation clips on tracks, with markers and the Chord Track on the ruler.', [43.6, 13, 56.1, 53.5]],
    ['Mixer (F9)', 'Inserts with faders, pan, sends and an effect chain for the selected insert.', [43.6, 67.2, 56.1, 29.6]],
  ];
  const tour = $('#tour-map'), card = $('#tour-card'), nav = $('.tour-nav', card);
  const zone = document.createElement('div');
  zone.className = 'zone';
  tour.appendChild(zone);
  let stop = -1;
  const spots = stops.map(([title], i) => {
    const [x, y, w, h] = stops[i][2];
    const b = document.createElement('button');
    b.className = 'spot';
    b.textContent = i + 1;
    b.style.left = `${x + w / 2}%`;
    b.style.top = `${y + h / 2}%`;
    b.setAttribute('aria-label', title);
    b.addEventListener('click', () => show(i));
    b.addEventListener('mouseenter', () => show(i));
    tour.appendChild(b);
    return b;
  });
  function show(i) {
    stop = (i + stops.length) % stops.length;
    const [title, text, [x, y, w, h]] = stops[stop];
    Object.assign(zone.style, { left: `${x}%`, top: `${y}%`, width: `${w}%`, height: `${h}%` });
    tour.classList.add('focus');
    spots.forEach((s, j) => s.classList.toggle('on', j === stop));
    card.innerHTML = `<b>${stop + 1}. ${esc(title)}</b>${esc(text)}`;
    card.appendChild(nav);
  }
  $('#tour-prev').addEventListener('click', () => show(stop - 1));
  $('#tour-next').addEventListener('click', () => show(stop + 1));
  tour.addEventListener('mouseleave', () => tour.classList.remove('focus'));

  // ---------- First-beat stepper ----------
  const articles = $$('#stepper article'), stepNav = $('.steps-nav');
  let current = 0;
  articles.forEach((a, i) => stepNav.insertAdjacentHTML('beforeend', `<button role="tab"><span>${i + 1}</span>${esc(a.dataset.title)}</button>`));
  const stepButtons = $$('button', stepNav);
  function step(i) {
    current = Math.max(0, Math.min(articles.length - 1, i));
    articles.forEach((a, j) => a.classList.toggle('on', j === current));
    stepButtons.forEach((b, j) => { b.classList.toggle('on', j === current); b.classList.toggle('done', j < current); });
    $('#step-bar').style.width = `${((current + 1) / articles.length) * 100}%`;
    $('#step-prev').disabled = current === 0;
    $('#step-next').textContent = current === articles.length - 1 ? 'Start again' : 'Next step';
  }
  stepButtons.forEach((b, i) => b.addEventListener('click', () => step(i)));
  $('#step-prev').addEventListener('click', () => step(current - 1));
  $('#step-next').addEventListener('click', () => step(current === articles.length - 1 ? 0 : current + 1));
  step(0);

  // ---------- Effects ----------
  const fxColours = { EQ: '#669ce3', Dynamics: '#29d7c5', Time: '#a48ce1', Modulation: '#db6b85', Distortion: '#eeb76a', Stereo: '#7fd6f0', Pitch: '#9be36b', Utility: '#95a9b4' };
  const effects = [
    ['Seven Band EQ', 'EQ', 'Fast tone shaping with seven fixed bands and an output trim.'],
    ['Parametric EQ', 'EQ', 'Low and high cut, two shelves and two bells for surgical mixing.'],
    ['Bass Shelf', 'EQ', 'Adds weight under kicks and basses in one knob.'],
    ['Filter', 'EQ', 'Low-pass, high-pass or band-pass with resonance; automate it for sweeps.'],
    ['Formant Filter', 'EQ', 'A E I O U vowel shapes that make synths talk.'],
    ['Compressor', 'Dynamics', 'Threshold, ratio, attack, release and makeup, with parallel mix.'],
    ['Peak Limiter', 'Dynamics', 'A zero-lookahead ceiling that catches stray peaks.'],
    ['Maximizer', 'Dynamics', 'Loudness for the Master: push gain into a clean ceiling.'],
    ['Gate', 'Dynamics', 'Removes noise and tails below a threshold.'],
    ['De-esser', 'Dynamics', 'Tames harsh S sounds on vocals, with a listen mode.'],
    ['Transient Shaper', 'Dynamics', 'More snap or more sustain on drums without compression.'],
    ['Multiband Dynamics', 'Dynamics', 'Three-band upward and downward compression for glue and polish.'],
    ['Pump', 'Dynamics', 'Tempo-synced volume pumping with curve, hold and offset.'],
    ['Stereo Delay', 'Time', 'Echoes with feedback, crossfeed and a darker feedback path.'],
    ['Room Reverb', 'Time', 'A light, quick room for drums and small spaces.'],
    ['Studio Reverb', 'Time', 'Room, plate or hall with size, diffusion, modulation and EQ.'],
    ['Convolution Reverb', 'Time', 'Real spaces from built-in rooms or your own impulse files.'],
    ['Beat Gate', 'Time', 'Tempo-synced gate patterns, stutter and half-time that follow the song.'],
    ['Chorus', 'Modulation', 'Widens and thickens pads, keys and guitars.'],
    ['Flanger', 'Modulation', 'The classic jet sweep with feedback.'],
    ['Phaser', 'Modulation', 'Swirling notches for movement.'],
    ['Tremolo', 'Modulation', 'Rhythmic volume from sine to square.'],
    ['Ring Mod', 'Modulation', 'Metallic, bell-like tones.'],
    ['Auto Pan', 'Modulation', 'Moves the sound between the speakers.'],
    ['Frequency Shifter', 'Modulation', 'Inharmonic shifts and barber-pole feedback.'],
    ['Soft Clip', 'Distortion', 'Rounds peaks for safe extra loudness.'],
    ['Drive', 'Distortion', 'Simple grit and edge.'],
    ['Crush', 'Distortion', 'Bit depth and sample-rate reduction for retro textures.'],
    ['Saturator', 'Distortion', 'Tape-to-tube warmth with tone control.'],
    ['Lo-fi', 'Distortion', 'Tape wow, flutter, hiss and vinyl crackle.'],
    ['Waveshaper', 'Distortion', 'Soft, hard, fold, tube and rectify shapes with bias.'],
    ['Stereo Width', 'Stereo', 'From mono to extra wide.'],
    ['Stereo Matrix', 'Stereo', 'Route and mix left and right freely.'],
    ['Tune', 'Pitch', 'Pitch correction to major, minor, pentatonic or maqam scales.'],
    ['Vocoder', 'Pitch', 'Robot voices from a carrier note and noise.'],
    ['Balance', 'Utility', 'Gain and pan in one place.'],
    ['Polarity', 'Utility', 'Invert left or right to fix phase.'],
    ['Mute', 'Utility', 'An automatable mute.'],
    ['DC Block', 'Utility', 'Removes DC offset and sub rumble.'],
    ['Spectrum Analyzer', 'Utility', 'See the frequency balance of any track.'],
  ];
  const fxFilter = $('#fx-filter'), fxGrid = $('#fx-grid');
  const cats = ['All', ...new Set(effects.map(e => e[1]))];
  let fxCat = 'All';
  cats.forEach(c => fxFilter.insertAdjacentHTML('beforeend',
    `<button data-cat="${c}" class="${c === 'All' ? 'on' : ''}">${c}<em>${c === 'All' ? effects.length : effects.filter(e => e[1] === c).length}</em></button>`));
  function renderFx() {
    fxGrid.innerHTML = effects.filter(e => fxCat === 'All' || e[1] === fxCat).map(([name, cat, text]) =>
      `<div class="fx" style="--c:${fxColours[cat]}"><span class="cat">${cat}</span><h4>VibeRa ${esc(name)}</h4><p>${esc(text)}</p></div>`).join('');
  }
  fxFilter.addEventListener('click', e => {
    const b = e.target.closest('button'); if (!b) return;
    fxCat = b.dataset.cat;
    $$('button', fxFilter).forEach(x => x.classList.toggle('on', x === b));
    renderFx();
  });
  renderFx();

  // ---------- Performance pads (a little follow-action demo) ----------
  const padNames = [['Intro', '#a48ce1'], ['Verse', '#29d7c5'], ['Chorus', '#db6b85'], ['Break', '#eeb76a'], ['Drop', '#669ce3'], ['Fill', '#e7c56c'], ['Outro', '#9be36b'], ['Stop', '#95a9b4']];
  const pads = $('#pads');
  const padEls = padNames.map(([n, c]) => { const d = document.createElement('div'); d.className = 'pad'; d.style.setProperty('--c', c); d.textContent = n; pads.appendChild(d); return d; });
  const sequence = [0, 0, 1, 1, 2, 2, 3, 4, 4, 5, 6];
  let seqAt = 0;
  function padTick() {
    padEls.forEach(p => p.classList.remove('live', 'queued'));
    padEls[sequence[seqAt]].classList.add('live');
    padEls[sequence[(seqAt + 1) % sequence.length]].classList.add('queued');
    seqAt = (seqAt + 1) % sequence.length;
  }
  padEls.forEach((p, i) => p.addEventListener('click', () => { seqAt = Math.max(0, sequence.indexOf(i)); padTick(); }));
  padTick();
  if (!reduced) setInterval(padTick, 1600);

  // ---------- Chord Track demo ----------
  const chords = [['Am', 'A C E'], ['F', 'F A C'], ['C', 'C E G'], ['G', 'G B D'], ['Am', 'A C E'], ['F', 'F A C'], ['C', 'C E G'], ['G', 'G B D']];
  const chordbar = $('#chordbar');
  chords.forEach(([s, n], i) => chordbar.insertAdjacentHTML('beforeend', `<div>${s}<small>bar ${i + 1} &middot; ${n}</small></div>`));
  const chordEls = $$('div', chordbar);
  let chordAt = 0;
  const chordTick = () => { chordEls.forEach((c, i) => c.classList.toggle('now', i === chordAt)); chordAt = (chordAt + 1) % chordEls.length; };
  chordTick();
  if (!reduced) setInterval(chordTick, 1000);

  // ---------- Maqam keyboard (24 quarter tones from C) ----------
  const maqams = {
    Rast: [0, 4, 7, 10, 14, 18, 21], Bayati: [0, 3, 6, 10, 14, 16, 20], Saba: [0, 3, 6, 8, 14, 16, 20],
    Hijaz: [0, 2, 8, 10, 14, 16, 20], Nahawand: [0, 4, 6, 10, 14, 16, 22], Kurd: [0, 2, 6, 10, 14, 16, 20],
  };
  const names = ['C', 'C#', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A', 'Bb', 'B'];
  const mkeys = $('#maqam-keys'), mpick = $('#maqam-pick');
  const cells = Array.from({ length: 24 }, (_, i) => { const d = document.createElement('div'); d.textContent = i % 2 ? '½' : names[i / 2]; mkeys.appendChild(d); return d; });
  function maqam(name) {
    const set = maqams[name];
    cells.forEach((c, i) => { c.className = set.includes(i) ? (i % 2 ? 'qt' : 'in') : ''; });
    $$('button', mpick).forEach(b => b.classList.toggle('on', b.textContent === name));
  }
  Object.keys(maqams).forEach(n => mpick.insertAdjacentHTML('beforeend', `<button>${n}</button>`));
  mpick.addEventListener('click', e => { if (e.target.tagName === 'BUTTON') maqam(e.target.textContent); });
  maqam('Rast');

  // ---------- Loudness needle ----------
  const needle = $('#lufs i'), lufsLabel = $('#lufs span');
  if (!reduced) setInterval(() => {
    const v = -14 + (Math.random() - 0.5) * 1.6;
    needle.style.left = `${55 + (v + 16) * 4}%`;
    lufsLabel.textContent = `${v.toFixed(1)} LUFS`;
  }, 400);

  // ---------- Shortcuts ----------
  const shortcuts = {
    Global: [['Space', 'Play / pause'], ['R', 'Record'], ['L', 'Pattern / Song mode'], ['Home', 'Go to start'], ['End', 'Go to end'],
      ['Ctrl + M', 'Metronome'], ['Ctrl + P', 'Count-in'], ['Ctrl + T', 'Typing keyboard'], ['Ctrl + S', 'Save'], ['Ctrl + O', 'Open'],
      ['Ctrl + R', 'Export'], ['Ctrl + Z', 'Undo'], ['Ctrl + Y', 'Redo'], ['Ctrl + Shift + Z', 'Redo'], ['F5', 'Playlist'], ['F6', 'Channel Rack'],
      ['F7', 'Piano roll'], ['F8', 'Plugin browser'], ['F9', 'Mixer'], ['F10', 'Settings'], ['F2', 'Rename Pattern'], ['F4', 'Next empty Pattern'],
      ['Shift + F4', 'First empty Pattern'], ['Ctrl + Shift + P', 'Pattern time signature'], ['Esc', 'Cancel selection']],
    'Channel Rack': [['Ctrl + K', 'Graph editor'], ['F2', 'Channel name and colour'], ['Alt + C', 'Clone Channel'], ['Alt + Del', 'Delete Channel'],
      ['Alt + Up', 'Move Channel up'], ['Alt + Down', 'Move Channel down'], ['Ctrl + Shift + Left', 'Rotate steps left'], ['Ctrl + Shift + Right', 'Rotate steps right'],
      ['Ctrl + L', 'Link to selected Mixer insert'], ['Ctrl + Shift + L', 'Link to consecutive inserts'], ['Alt + Z', 'Zip selected Channels'], ['Alt + U', 'Unzip all'],
      ['Alt + G', 'Group Channels'], ['PgUp', 'Previous group'], ['PgDn', 'Next group'], ['Ctrl + C', 'Copy steps'], ['Ctrl + V', 'Paste steps'],
      ['Ctrl + Shift + Up', 'Move Pattern up'], ['Ctrl + Shift + Down', 'Move Pattern down'], ['Ctrl + A', 'Select all'], ['Ctrl + D', 'Select none']],
    'Piano roll': [['P', 'Draw'], ['B', 'Paint'], ['C', 'Slice'], ['T', 'Mute tool'], ['N', 'Drum paint'], ['Y', 'Play tool'], ['S', 'Insert slide'],
      ['O', 'Insert portamento'], ['Alt + V', 'Ghost notes'], ['Ctrl + Alt + V', 'Edit ghosts'], ['Shift + Up', 'Transpose up'], ['Shift + Down', 'Transpose down'],
      ['Ctrl + Up', 'Octave up'], ['Ctrl + Down', 'Octave down'], ['Shift + Left', 'Nudge left'], ['Shift + Right', 'Nudge right'], ['Ctrl + Q', 'Quick quantize'],
      ['Shift + Q', 'Quantize starts'], ['Alt + Q', 'Quantizer'], ['Ctrl + L', 'Legato'], ['Ctrl + U', 'Chop'], ['Ctrl + G', 'Glue'], ['Ctrl + B', 'Duplicate'],
      ['Shift + G', 'Group notes'], ['Alt + G', 'Ungroup'], ['Alt + M', 'Mute notes'], ['Alt + Shift + M', 'Unmute notes'], ['Shift + R', 'Select random'],
      ['Shift + O', 'Select odd'], ['Shift + I', 'Invert selection'], ['Shift + D', 'Discard lengths'], ['Ctrl + Shift + Left', 'Rotate notes left'],
      ['Ctrl + Shift + Right', 'Rotate notes right'], ['Ctrl + Ins', 'Insert space'], ['Ctrl + Del', 'Delete space'], ['Ctrl + I', 'Insert controller value'],
      ['Ctrl + Alt + Y', 'Run last script again'], ['Ctrl + Shift + V', 'Paste MIDI clipboard'], ['Ctrl + Enter', 'Select time around notes'],
      ['Alt + B', 'Black-key row shading'], ['Alt + C', 'Change note colour'], ['Shift + C', 'Select by colour'], ['PgUp', 'Zoom in'], ['PgDn', 'Zoom out'],
      ['Shift + 4', 'Fit'], ['Shift + 5', 'Zoom to selection'], ['Ctrl + Alt + Home', 'Resize from the left'], ['Del', 'Delete notes']],
    Playlist: [['P', 'Draw'], ['B', 'Paint'], ['C', 'Slice'], ['S', 'Slip'], ['E', 'Select'], ['D', 'Delete tool'], ['T', 'Mute tool'], ['Y', 'Play tool'],
      ['Shift + Z', 'Zoom tool'], ['Alt + P', 'Clip picker'], ['Shift + G', 'Group clips'], ['Alt + G', 'Ungroup clips'], ['7', 'Audio clip pitch down'],
      ['8', 'Audio clip pitch up'], ['9', 'Reverse audio clip'], ['Ctrl + B', 'Duplicate'], ['Ctrl + G', 'Merge Pattern clips'], ['Ctrl + Shift + G', 'Merge similar everywhere'],
      ['Ctrl + Q', 'Quantize clip starts'], ['Ctrl + Ins', 'Insert time'], ['Ctrl + Del', 'Delete time'], ['Shift + Up', 'Clip to track above'],
      ['Shift + Down', 'Clip to track below'], ['Ctrl + Up', 'Previous track'], ['Ctrl + Down', 'Next track'], ['Ctrl + Shift + B', 'Clone track'],
      ['Shift + M', 'Stretch mode'], ['Alt + T', 'Marker at playhead'], ['PgUp', 'Zoom in'], ['PgDn', 'Zoom out'], ['Shift + 1', 'Zoom preset 1'],
      ['Shift + 4', 'Fit song'], ['Shift + 5', 'Zoom to selection'], ['Del', 'Delete clips']],
    Mixer: [['F2', 'Rename track'], ['Ctrl + A', 'Select all'], ['Alt + Left', 'Move track left'], ['Alt + Right', 'Move track right'], ['Alt + G', 'Group tracks'],
      ['Alt + L', 'Select routed Channels'], ['Alt + W', 'Meter-wave view'], ['Ctrl + L', 'Link selected Channels']],
    Browser: [['Backspace', 'Back'], ['Shift + Backspace', 'Forward'], ['Alt + Left', 'Back'], ['Alt + Right', 'Forward'], ['Alt + Up', 'Up a folder'],
      ['Ctrl + F', 'Find'], ['F3', 'Next result'], ['Shift + F3', 'Previous result'], ['Ctrl + R', 'Reread folders']],
  };
  const scTabs = $('#sc-tabs'), scList = $('#sc-list'), scFilter = $('#sc-filter');
  let scArea = 'Global';
  ['All', ...Object.keys(shortcuts)].forEach(a => scTabs.insertAdjacentHTML('beforeend', `<button class="${a === scArea ? 'on' : ''}">${a}</button>`));
  function renderShortcuts() {
    const q = scFilter.value.trim().toLowerCase();
    const rows = Object.entries(shortcuts).flatMap(([area, list]) => list.map(([k, d]) => [area, k, d]))
      .filter(([area, k, d]) => (q ? true : scArea === 'All' || area === scArea) && (!q || `${k} ${d} ${area}`.toLowerCase().includes(q)));
    scList.innerHTML = rows.map(([area, k, d]) => `<div class="sc"><span>${esc(d)}${q || scArea === 'All' ? `<em>${area}</em>` : ''}</span><span class="sc-keys">${keys(k)}</span></div>`).join('')
      || '<p class="muted">No shortcut matches.</p>';
  }
  scTabs.addEventListener('click', e => {
    if (e.target.tagName !== 'BUTTON') return;
    scArea = e.target.textContent;
    $$('button', scTabs).forEach(b => b.classList.toggle('on', b === e.target));
    renderShortcuts();
  });
  scFilter.addEventListener('input', renderShortcuts);
  renderShortcuts();

  // ---------- Hacks ----------
  const hacks = [
    ['Rack', 'Ctrl+Shift+Left / Right rotates the selected Channel\'s steps: one hat pattern becomes four grooves.'],
    ['Rack', 'Select every drum Channel and press Ctrl+Shift+L to give each its own Mixer insert in one move.'],
    ['Rack', 'Store in spare state, tweak, then flip: instant A/B of two versions of a sound.'],
    ['Rack', 'Ctrl+K opens the graph editor: lower the velocity of off-beat hats for an instant human feel.'],
    ['Piano', 'Shift+R selects random notes. Delete them, or lower their velocity, for variations that still sound like you.'],
    ['Piano', 'Shift+O selects every other note: a fast way to thin a busy hi-hat roll.'],
    ['Piano', 'Turn on a chord stamp and every note you draw becomes a full chord.'],
    ['Piano', 'Ctrl+Shift+V pastes notes copied as MIDI from another program.'],
    ['Piano', 'Ctrl+Alt+Y runs the last note script again on a new selection.'],
    ['Piano', 'Alt+V shows other Channels as ghost notes so the bass can follow the kick.'],
    ['Playlist', 'Ctrl+Ins on a ruler selection adds bars across the whole song without moving clips by hand.'],
    ['Playlist', 'Press 9 on an audio clip to reverse it: instant reverse-cymbal swells.'],
    ['Playlist', 'Ctrl+Shift+G merges similar Pattern clips everywhere, keeping the song tidy.'],
    ['Playlist', 'Add markers every 8 bars to see the song structure at a glance; Alt+T drops one at the playhead.'],
    ['Mixer', 'Put Pump on the bass for tempo-synced ducking without routing a sidechain.'],
    ['Mixer', 'Save a finished vocal insert as a track state and reuse the whole chain in your next song.'],
    ['Mixer', 'Touch-record fader moves while playing to automate a build-up in one pass.'],
    ['Mixer', 'Alt+W switches to the Meter-wave view to spot clipping tracks instantly.'],
    ['Song', 'Write the Chord Track once, then let a bassline follow the chords across the whole song.'],
    ['Song', 'Follow actions with Another Pattern (random) turn a few drum variations into an endless groove.'],
    ['Song', 'Record knob moves inside a Pattern: the sweep plays wherever that Pattern is placed.'],
    ['Browser', 'Search kick NOT (loop OR fx) to list only one-shot kicks, then save the search as a tab.'],
    ['Synth', 'Type wavetable in the Synth preset search to see every wavetable preset.'],
    ['Export', 'After export, read the loudness report and aim near -14 LUFS with true peak below -1 dBTP.'],
    ['Safety', 'Tools > Open project backups folder holds five rotating backups of every save.'],
    ['Speed', 'Shift+4 fits the song or Pattern on screen; Shift+5 zooms to the selection.'],
  ];
  const tipText = $('#tip-text');
  let lastTip = -1;
  function nextTip() {
    let i; do { i = Math.floor(Math.random() * hacks.length); } while (i === lastTip && hacks.length > 1);
    lastTip = i;
    tipText.style.opacity = 0;
    setTimeout(() => { tipText.textContent = hacks[i][1]; tipText.style.opacity = 1; }, reduced ? 0 : 180);
  }
  $('#tip-next').addEventListener('click', nextTip);
  nextTip();
  $('#hack-grid').innerHTML = hacks.map(([area, text]) => `<div class="hcard"><b>${area}</b><p>${esc(text)}</p></div>`).join('');

  // ---------- Search ----------
  const search = $('#search'), count = $('#search-count'), none = $('#no-results');
  const markable = 'p, li, dd, dt, h3, h4, figcaption, summary, .keygrid div';
  function clearMarks() {
    $$('mark', $('#content')).forEach(m => m.replaceWith(document.createTextNode(m.textContent)));
    $('#content').normalize();
  }
  function markWord(root, word) {
    const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
    const hits = [];
    while (walker.nextNode()) {
      const n = walker.currentNode;
      if (n.parentElement.closest('kbd, mark, button, script')) continue;
      if (n.textContent.toLowerCase().includes(word)) hits.push(n);
    }
    hits.forEach(n => {
      const text = n.textContent, lower = text.toLowerCase(), frag = document.createDocumentFragment();
      let at = 0, i;
      while ((i = lower.indexOf(word, at)) !== -1) {
        frag.append(text.slice(at, i));
        const m = document.createElement('mark'); m.textContent = text.slice(i, i + word.length); frag.append(m);
        at = i + word.length;
      }
      frag.append(text.slice(at));
      n.replaceWith(frag);
    });
  }
  let timer;
  search.addEventListener('input', () => { clearTimeout(timer); timer = setTimeout(runSearch, 120); });
  function runSearch() {
    clearMarks();
    const words = search.value.trim().toLowerCase().split(/\s+/).filter(w => w.length > 1);
    let shown = 0;
    sections.forEach(s => {
      const hay = `${s.textContent} ${s.dataset.keys || ''}`.toLowerCase();
      const ok = !words.length || words.every(w => hay.includes(w));
      s.classList.toggle('hidden', !ok);
      tocLinks.get(s.id)?.parentElement.classList.toggle('hidden', !ok);
      if (ok) { shown++; s.classList.add('in'); }
      if (ok && words.length) $$(markable, s).forEach(el => words.forEach(w => markWord(el, w)));
    });
    none.hidden = shown > 0;
    count.textContent = words.length ? `${shown} section${shown === 1 ? '' : 's'} match` : '';
  }
  addEventListener('keydown', e => {
    if (e.key === '/' && document.activeElement.tagName !== 'INPUT') { e.preventDefault(); search.focus(); search.scrollIntoView({ block: 'center' }); }
  });
  search.addEventListener('keydown', e => {
    if (e.key === 'Enter') { const first = sections.find(s => !s.classList.contains('hidden')); first?.scrollIntoView({ behavior: reduced ? 'auto' : 'smooth' }); }
    if (e.key === 'Escape') { search.value = ''; runSearch(); }
  });
})();
