// Novgorod in the browser: image re-encoding before upload, quoting,
// image expansion and post previews. The forum works without it, except
// for attaching images.
(() => {
  'use strict';

  const MAX_SIDE = 2048;     // longest side of an uploaded image
  const MAX_IMAGE = 2300000; // base64 characters the server accepts (2400000)

  function loadImage(file) {
    return new Promise((resolve, reject) => {
      const url = URL.createObjectURL(file);
      const img = new Image();
      img.onload = () => { URL.revokeObjectURL(url); resolve(img); };
      img.onerror = () => { URL.revokeObjectURL(url); reject(new Error('That file is not an image this browser can read.')); };
      img.src = url;
    });
  }

  // The image redrawn with its longest side at most `side` pixels. Drawing
  // it is what drops EXIF data (camera, GPS position) and anything hidden
  // after the picture.
  function draw(img, side, opaque) {
    const w0 = img.naturalWidth, h0 = img.naturalHeight;
    const scale = Math.min(1, side / Math.max(w0, h0));
    const canvas = document.createElement('canvas');
    canvas.width = Math.max(1, Math.round(w0 * scale));
    canvas.height = Math.max(1, Math.round(h0 * scale));
    const ctx = canvas.getContext('2d');
    if (opaque) {
      ctx.fillStyle = '#ffffff';
      ctx.fillRect(0, 0, canvas.width, canvas.height);
    }
    ctx.imageSmoothingQuality = 'high';
    ctx.drawImage(img, 0, 0, canvas.width, canvas.height);
    return canvas;
  }

  function base64(canvas, type, quality) {
    const url = canvas.toDataURL(type, quality);
    return url.slice(url.indexOf(',') + 1);
  }

  async function encode(file) {
    const img = await loadImage(file);
    if (!img.naturalWidth || !img.naturalHeight) throw new Error('That image is empty.');
    let canvas = null, data = null, mime = 'png';
    // screenshots and diagrams stay sharp as PNG when they fit
    if (file.type === 'image/png' || file.type === 'image/gif') {
      canvas = draw(img, MAX_SIDE, false);
      data = base64(canvas, 'image/png');
      if (data.length > MAX_IMAGE) data = null;
    }
    if (data === null) {
      mime = 'jpeg';
      let side = MAX_SIDE;
      for (let round = 0; round < 8 && data === null; round++) {
        canvas = draw(img, side, true);
        for (const quality of [0.9, 0.8, 0.7]) {
          const attempt = base64(canvas, 'image/jpeg', quality);
          if (attempt.length <= MAX_IMAGE) { data = attempt; break; }
        }
        side = Math.round(side * 0.75);
      }
      if (data === null) throw new Error('That image is too large even after shrinking it.');
    }
    // the server makes the thumbnail (Omsk)
    return { mime, img: data, imgw: canvas.width, imgh: canvas.height };
  }

  function setFields(form, values) {
    for (const name of ['mime', 'imgw', 'imgh', 'img']) {
      const input = form.elements.namedItem(name);
      if (input) input.value = values ? String(values[name]) : '';
    }
  }

  function setupForm(form) {
    const file = form.querySelector('input[data-image]');
    const status = form.querySelector('[data-status]');
    const button = form.querySelector('button[type=submit]');
    const text = form.querySelector('textarea');
    const say = (message, bad) => {
      if (!status) return;
      status.textContent = message;
      status.classList.toggle('bad', !!bad);
    };
    if (file) {
      file.addEventListener('change', () => {
        const f = file.files[0];
        if (f) say(`${f.name}: ${Math.round(f.size / 1024)} KiB, re-encoded when you post.`);
      });
    }
    form.addEventListener('submit', async (event) => {
      if (form.dataset.ready === '1') return;
      event.preventDefault();
      if (button.disabled) return;
      const f = file && file.files[0];
      button.disabled = true;
      try {
        if (f) {
          say('Encoding the image…');
          setFields(form, await encode(f));
        } else {
          setFields(form, null);
        }
        say('Posting…');
        form.dataset.ready = '1';
        HTMLFormElement.prototype.submit.call(form);
      } catch (err) {
        say(err.message || String(err), true);
        button.disabled = false;
      }
    });
    if (text) {
      text.addEventListener('keydown', (event) => {
        if (event.key === 'Enter' && (event.ctrlKey || event.metaKey)) {
          event.preventDefault();
          form.requestSubmit ? form.requestSubmit() : button.click();
        }
      });
    }
    // coming back with the browser's back button
    window.addEventListener('pageshow', () => {
      form.dataset.ready = '';
      button.disabled = false;
    });
  }

  function replyForm() {
    const form = document.querySelector('form.post-form');
    if (!form) return null;
    const thread = form.elements.namedItem('thread');
    return thread && thread.value !== '0' ? form : null;
  }

  function quote(no) {
    const form = replyForm();
    if (!form) return false;
    const text = form.querySelector('textarea');
    const insert = `>>${no}\n`;
    const at = text.selectionStart ?? text.value.length;
    const before = text.value.slice(0, at);
    const pad = before && !before.endsWith('\n') ? '\n' : '';
    text.value = before + pad + insert + text.value.slice(text.selectionEnd ?? at);
    const caret = (before + pad + insert).length;
    text.focus();
    text.setSelectionRange(caret, caret);
    return true;
  }

  function toggleImage(link) {
    const img = link.querySelector('img');
    if (!img) return;
    if (!link.dataset.thumb) {
      link.dataset.thumb = img.getAttribute('src');
      link.dataset.tw = img.getAttribute('width');
      link.dataset.th = img.getAttribute('height');
    }
    const expanded = link.classList.toggle('expanded');
    if (expanded) {
      img.src = link.getAttribute('href');
      img.width = Number(link.dataset.w);
      img.height = Number(link.dataset.h);
    } else {
      img.src = link.dataset.thumb;
      img.width = Number(link.dataset.tw);
      img.height = Number(link.dataset.th);
    }
  }

  let preview = null;
  function showPreview(link, event) {
    hidePreview();
    const post = document.getElementById(`p${link.dataset.ref}`);
    if (!post || post.contains(link)) return;
    preview = post.cloneNode(true);
    preview.removeAttribute('id');
    preview.classList.add('preview');
    document.body.appendChild(preview);
    const pad = 12;
    const width = Math.min(preview.offsetWidth, window.innerWidth - 2 * pad);
    let x = event.clientX + pad;
    if (x + width > window.innerWidth - pad) x = Math.max(pad, window.innerWidth - width - pad);
    let y = event.clientY + pad;
    if (y + preview.offsetHeight > window.innerHeight - pad) y = Math.max(pad, event.clientY - preview.offsetHeight - pad);
    preview.style.left = `${x + window.scrollX}px`;
    preview.style.top = `${y + window.scrollY}px`;
  }
  function hidePreview() {
    if (preview) { preview.remove(); preview = null; }
  }

  document.addEventListener('click', (event) => {
    const target = event.target.closest('a, .spoiler');
    if (!target) return;
    if (target.matches('a.quote-me') && quote(target.dataset.quote)) {
      event.preventDefault();
    } else if (target.matches('a[data-expand]') && !event.ctrlKey && !event.metaKey && !event.shiftKey) {
      event.preventDefault();
      toggleImage(target);
    } else if (target.matches('a[data-back]') && history.length > 1) {
      event.preventDefault();
      history.back();
    } else if (target.matches('.spoiler')) {
      target.classList.toggle('shown');
    }
  });
  document.addEventListener('mouseover', (event) => {
    const link = event.target.closest('a.ref[data-ref]');
    if (link) showPreview(link, event);
  });
  document.addEventListener('mouseout', (event) => {
    if (event.target.closest('a.ref[data-ref]')) hidePreview();
  });

  document.querySelectorAll('form.post-form').forEach(setupForm);
  // /c/res/1#q12 from a board page: quote post 12
  const wanted = /^#q(\d+)$/.exec(location.hash);
  if (wanted) {
    const post = document.getElementById(`p${wanted[1]}`);
    if (post) post.scrollIntoView();
    quote(wanted[1]);
  }
  const target = /^#p(\d+)$/.exec(location.hash);
  if (target) {
    const post = document.getElementById(`p${target[1]}`);
    if (post) post.classList.add('target');
  }
})();
