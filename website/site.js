// The living studio in the hero, the maqam keyboard and the release list.
(() => {
  const reduced = window.matchMedia('(prefers-reduced-motion: reduce)').matches;

  // Playlist: four tracks with clips; a playhead sweeps 16 bars.
  const lanes = document.getElementById('lanes');
  const ruler = document.getElementById('ruler');
  for (let bar = 1; bar <= 16; bar++) ruler.insertAdjacentHTML('beforeend', `<span>${bar % 4 === 1 ? bar : ''}</span>`);
  const tracks = [
    ['Drums', '#db6b85', [[0, 4], [4, 4], [8, 4], [12, 4]]],
    ['Bass', '#eeb76a', [[0, 8], [8, 8]]],
    ['Keys', '#a48ce1', [[4, 4], [12, 4]]],
    ['Lead', '#29d7c5', [[8, 2], [10, 2], [12, 4]]],
  ];
  for (const [name, colour, clips] of tracks) {
    const lane = document.createElement('div');
    lane.className = 'lane';
    lane.innerHTML = `<div class="lane-name">${name}</div><div class="lane-track"></div>`;
    const track = lane.querySelector('.lane-track');
    for (const [start, length] of clips)
      track.insertAdjacentHTML('beforeend',
        `<div class="clip" style="left:${start / 16 * 100}%;width:calc(${length / 16 * 100}% - 3px);background:${colour}"></div>`);
    lanes.appendChild(lane);
  }

  // Channel Rack: classic four-on-the-floor with off-beat hats and a clap.
  const rack = document.getElementById('rack');
  const rows = [
    ['KICK', '#db6b85', [0, 4, 8, 12]],
    ['CLAP', '#eeb76a', [4, 12]],
    ['HAT', '#e7c56c', [2, 6, 10, 14]],
    ['PERC', '#669ce3', [3, 7, 11, 13, 15]],
  ];
  const stepCells = [];
  for (const [name, colour, on] of rows) {
    const row = document.createElement('div');
    row.className = 'rack-row';
    row.style.setProperty('--c', colour);
    row.innerHTML = `<b>${name}</b>` + Array.from({ length: 16 }, (_, i) => `<div class="step${on.includes(i) ? ' on' : ''}"></div>`).join('');
    rack.appendChild(row);
    stepCells.push([...row.querySelectorAll('.step')]);
  }

  // Mixer meters.
  const meters = document.getElementById('meters');
  const bars = Array.from({ length: 8 }, () => {
    const m = document.createElement('div');
    m.className = 'meter';
    m.innerHTML = '<i></i>';
    meters.appendChild(m);
    return m.firstChild;
  });

  const playhead = document.getElementById('playhead');
  const clock = document.getElementById('clock');
  const bpm = 128, beatMs = 60000 / bpm;
  let start = performance.now();
  function frame(now) {
    const beats = (now - start) / beatMs;
    const loopBeats = beats % 64; // 16 bars of 4/4
    const width = lanes.clientWidth - 98;
    playhead.style.left = `${88 + width * loopBeats / 64}px`;
    const bar = Math.floor(loopBeats / 4) + 1, beat = Math.floor(loopBeats % 4) + 1;
    clock.textContent = `${String(bar).padStart(2, '0')}:${beat}:${String(Math.floor((loopBeats % 1) * 1000)).padStart(3, '0')}`;
    const step = Math.floor(beats * 4) % 16;
    stepCells.forEach(cells => cells.forEach((cell, i) => cell.classList.toggle('hit', i === step && cell.classList.contains('on'))));
    const pulse = 1 - (beats % 1);
    bars.forEach((b, i) => {
      const level = 22 + 60 * pulse * (0.55 + 0.45 * Math.abs(Math.sin(beats * 0.7 + i))) + 10 * Math.random();
      b.style.height = `${Math.min(98, level)}%`;
    });
    if (!reduced) requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);

  // Maqam Rast on C: the guide's tinted rows, quarter tones in orange.
  const keys = document.getElementById('keys');
  const rast = [['C', 'tonic'], ['C#', ''], ['D', 'on'], ['E♭', ''], ['E ½♭', 'quarter'], ['F', 'on'], ['F#', ''],
                ['G', 'on'], ['G#', ''], ['A', 'on'], ['B♭', ''], ['B ½♭', 'quarter']].reverse();
  rast.forEach(([name, kind], i) => {
    const row = document.createElement('div');
    row.className = 'key-row' + (kind === 'quarter' ? ' quarter' : kind ? ' on' : '');
    const left = (i * 7) % 60, width = kind ? 18 + (i % 3) * 8 : 0;
    row.innerHTML = `<span>${name}</span><div class="key-bar">${width ? `<i style="left:${left}%;width:${width}%"></i>` : ''}</div>`;
    keys.appendChild(row);
  });

  // Releases from GitHub.
  const list = document.getElementById('releases');
  const latestLabel = document.getElementById('latest-version');
  fetch('https://api.github.com/repos/Sheekovic/VibeRa-Studio/releases?per_page=6')
    .then(r => (r.ok ? r.json() : Promise.reject(r.status)))
    .then(releases => {
      if (!releases.length) {
        list.innerHTML = `<div class="release latest"><div><h3>First release coming soon</h3>
          <div class="meta">Builds are produced automatically on GitHub. Watch the repository to be notified.</div></div>
          <div class="assets"><a href="https://github.com/Sheekovic/VibeRa-Studio">Watch on GitHub</a></div></div>`;
        return;
      }
      latestLabel.textContent = `Latest: ${releases[0].tag_name} · early preview · AGPL-3.0`;
      list.innerHTML = releases.map((rel, i) => {
        const date = new Date(rel.published_at || rel.created_at).toLocaleDateString(undefined, { year: 'numeric', month: 'long', day: 'numeric' });
        const files = (rel.assets || []).map(a => {
          const setup = /setup\.exe$/i.test(a.name);
          return `<a class="${setup ? '' : 'alt'}" href="${a.browser_download_url}">${setup ? 'Installer' : a.name.endsWith('.zip') ? 'Portable ZIP' : a.name}</a>`;
        }).join('');
        return `<div class="release${i === 0 ? ' latest' : ''}"><div><h3>${rel.name || rel.tag_name}${i === 0 ? '<span class="badge">Latest</span>' : ''}</h3>
          <div class="meta">${date} · <a href="${rel.html_url}">Release notes</a></div></div>
          <div class="assets">${files || `<a href="${rel.html_url}">View</a>`}</div></div>`;
      }).join('');
    })
    .catch(() => {
      list.innerHTML = `<div class="release latest"><div><h3>See all releases on GitHub</h3></div>
        <div class="assets"><a href="https://github.com/Sheekovic/VibeRa-Studio/releases">Open releases</a></div></div>`;
    });
})();
