import * as THREE from 'three';
import { EffectComposer }  from 'three/examples/jsm/postprocessing/EffectComposer.js';
import { RenderPass }      from 'three/examples/jsm/postprocessing/RenderPass.js';
import { UnrealBloomPass } from 'three/examples/jsm/postprocessing/UnrealBloomPass.js';

let _renderer = null;
let _composer = null;
let _animId   = null;
let _net, _camera;
let _scene    = null;

const TOWER_REFS       = [];
const UE_GROUPS        = [];
const HOLO_RINGS       = [];
const HANDOVER_FLASHES = [];
const GROUND_CONNS     = [];
const SCAN_RINGS       = [];
const ARC_PACKETS      = [];
const COVERAGE_RINGS   = [];

let _ueTargets = null;
let _simMaxX   = 2000;
let _simMaxY   = 2000;

export function setUEPositions(ues, maxX, maxY, cells) {
  if (!ues || ues.length === 0) { _ueTargets = null; return; }
  _simMaxX = maxX || 2000;
  _simMaxY = maxY || 2000;
  const SCALE = 180;
  _ueTargets = ues.map(u => {
    const cellId = u.MMWave_Cell ?? u.LTE_Cell ?? 0;
    let towerX = 0, towerY = 21, towerZ = 0, cellColor = 0x00ccff;
    if (cells && cellId > 0 && cellId <= cells.length) {
      const c = cells[cellId - 1];
      towerX = c.x; towerZ = c.z;
      towerY = c.type === 'lte' ? 57 : 30;
      cellColor = parseInt((c.color || '#00ccff').replace('#', ''), 16);
    }
    return {
      x: (u.x_position - _simMaxX / 2) / (_simMaxX / 2) * SCALE,
      z: (u.y_position - _simMaxY / 2) / (_simMaxY / 2) * SCALE,
      servingCell: cellId, towerX, towerY, towerZ, cellColor,
    };
  });
}

export function flashUELabel(ueIndex) {
  const u = UE_GROUPS[ueIndex];
  if (!u || !u.labelSpr) return;
  let flashes = 0;
  const orig = u.labelSpr.material.opacity;
  const blink = setInterval(() => {
    u.labelSpr.material.opacity = flashes % 2 === 0 ? 1.0 : 0.2;
    if (++flashes >= 8) { clearInterval(blink); u.labelSpr.material.opacity = orig; }
  }, 120);
}

export function triggerHandover(fromX, fromZ, toX, toZ) {
  if (!_scene) return;

  // Expanding rings at destination
  for (let i = 0; i < 3; i++) {
    const col = [0x00ff88, 0xffcc00, 0x00ccff][i];
    const mat = new THREE.MeshBasicMaterial({
      color: col, transparent: true, opacity: 0.85,
      blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide,
    });
    const ring = new THREE.Mesh(new THREE.RingGeometry(1, 2, 32), mat);
    ring.rotation.x = -Math.PI / 2;
    ring.position.set(toX, 0.3 + i * 0.1, toZ);
    ring.scale.set(0.01, 0.01, 1);
    _scene.add(ring);
    HANDOVER_FLASHES.push({ mesh: ring, mat, startMs: performance.now() + i * 200, isRing: true });
  }

  // Bright arc between the two towers
  const dist = Math.hypot(toX - fromX, toZ - fromZ);
  const peak = Math.max(55, dist * 0.65);
  const curve = new THREE.QuadraticBezierCurve3(
    new THREE.Vector3(fromX, 30, fromZ),
    new THREE.Vector3((fromX + toX) / 2, peak, (fromZ + toZ) / 2),
    new THREE.Vector3(toX, 30, toZ)
  );
  const arcMat = new THREE.MeshBasicMaterial({ color: 0xffffff, transparent: true, opacity: 0.88,
    blending: THREE.AdditiveBlending, depthWrite: false });
  const arcMesh = new THREE.Mesh(new THREE.TubeGeometry(curve, 12, 0.22, 4), arcMat);
  _scene.add(arcMesh);
  HANDOVER_FLASHES.push({ mesh: arcMesh, mat: arcMat, startMs: performance.now(), isArc: true });
}

function loadHex(v) {
  return v < 0.45 ? 0x00ff88 : v < 0.70 ? 0xffcc00 : v < 0.88 ? 0xff6600 : 0xff2244;
}

const M_STEEL = new THREE.MeshStandardMaterial({ color: 0x8898a8, metalness: 0.88, roughness: 0.22 });

function tube(G, x1, y1, z1, x2, y2, z2, r, mat) {
  const p1 = new THREE.Vector3(x1, y1, z1);
  const p2 = new THREE.Vector3(x2, y2, z2);
  const dir = p2.clone().sub(p1);
  const len = dir.length();
  if (len < 0.005) return;
  const cyl = new THREE.Mesh(new THREE.CylinderGeometry(r, r, len, 5), mat);
  cyl.position.copy(p1.clone().add(p2).multiplyScalar(0.5));
  cyl.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), dir.normalize());
  G.add(cyl);
}

// ── Realistic parabolic dish antenna ──────────────────────────────────────────
function buildParaDish(G, cx, cy, cz, faceY, R, ledRefs, cellColor) {
  const dist   = Math.sqrt(cx * cx + cz * cz);
  if (dist < 0.001) return;
  const depth  = R * 0.38;                        // deeper bowl → clearly parabolic
  const F      = (R * R) / (4 * depth);
  const focalZ = F - depth;                       // focal point in front of aperture

  // off-white aluminium like a real telecom dish
  const dishMat  = new THREE.MeshStandardMaterial({ color: 0xdde8f0, metalness: 0.72, roughness: 0.10, side: THREE.DoubleSide });
  const rimMat   = new THREE.MeshStandardMaterial({ color: 0x8899aa, metalness: 0.95, roughness: 0.08 });
  const strutMat = new THREE.MeshStandardMaterial({ color: 0x778899, metalness: 0.90, roughness: 0.15 });
  const feedMat  = new THREE.MeshStandardMaterial({ color: 0xaabbcc, metalness: 0.90, roughness: 0.10 });
  const hubMat   = new THREE.MeshStandardMaterial({ color: 0x3a4a58, metalness: 0.88, roughness: 0.22 });

  const dG = new THREE.Group();
  dG.position.set(cx, cy, cz);
  dG.rotation.y = faceY;
  G.add(dG);

  // ── Paraboloid surface via LatheGeometry ──
  // Profile swept rim→centre (reversed) so face-normals point INTO the concave side
  const pts = [];
  for (let i = 32; i >= 0; i--) {
    const r = (i / 32) * R;
    pts.push(new THREE.Vector2(r, depth * ((r / R) * (r / R) - 1)));
  }
  const dishMesh = new THREE.Mesh(new THREE.LatheGeometry(pts, 64), dishMat);
  dishMesh.rotation.x = Math.PI / 2;   // concave opening → +Z
  dG.add(dishMesh);

  // ── Rim ring ──
  const rimRing = new THREE.Mesh(new THREE.TorusGeometry(R, 0.030 * R, 8, 56), rimMat);
  rimRing.rotation.x = Math.PI / 2;
  dG.add(rimRing);

  // ── 4 spider struts from rim to feed hub ──
  const up = new THREE.Vector3(0, 1, 0);
  for (let a = 0; a < 4; a++) {
    const ang = a * Math.PI / 2 + Math.PI / 4;
    const sx = Math.cos(ang) * R * 0.86, sy = Math.sin(ang) * R * 0.86;
    const dir = new THREE.Vector3(-sx, -sy, focalZ);
    const len = dir.length();
    const strut = new THREE.Mesh(
      new THREE.CylinderGeometry(0.018 * R, 0.018 * R, len, 6), strutMat);
    strut.position.set(sx / 2, sy / 2, focalZ / 2);
    strut.quaternion.setFromUnitVectors(up, dir.normalize());
    dG.add(strut);
  }

  // ── Feed horn at focal point ──
  // Flare cone (open toward dish)
  const flL = 0.26 * R;
  const flare = new THREE.Mesh(
    new THREE.CylinderGeometry(0.06 * R, 0.20 * R, flL, 14, 1, true), feedMat);
  flare.rotation.x = Math.PI / 2;
  flare.position.set(0, 0, focalZ - flL / 2);
  dG.add(flare);
  // Waveguide body behind the flare
  const wgL = 0.32 * R;
  const wg = new THREE.Mesh(
    new THREE.CylinderGeometry(0.062 * R, 0.062 * R, wgL, 10), feedMat);
  wg.rotation.x = Math.PI / 2;
  wg.position.set(0, 0, focalZ + wgL / 2);
  dG.add(wg);
  // LNB housing
  const lnbS = 0.22 * R;
  const lnb = new THREE.Mesh(new THREE.BoxGeometry(lnbS, lnbS, 0.28 * R), hubMat);
  lnb.position.set(0, 0, focalZ + wgL + 0.14 * R);
  dG.add(lnb);
  // Glow at feed aperture
  if (ledRefs && cellColor !== undefined) {
    const glowM = new THREE.MeshBasicMaterial({
      color: cellColor, transparent: true, opacity: 0.75, blending: THREE.AdditiveBlending });
    ledRefs.push(glowM);
    const glowSph = new THREE.Mesh(new THREE.SphereGeometry(0.080 * R, 8, 6), glowM);
    glowSph.position.set(0, 0, focalZ - 0.06 * R);
    dG.add(glowSph);
  }

  // ── Back: centre hub + 4 radial ribs ──
  const backHub = new THREE.Mesh(
    new THREE.CylinderGeometry(0.11 * R, 0.11 * R, 0.14 * R, 8), hubMat);
  backHub.rotation.x = Math.PI / 2;
  backHub.position.set(0, 0, -depth + 0.02);
  dG.add(backHub);

  for (let a = 0; a < 4; a++) {
    const ang    = a * Math.PI / 2 + Math.PI / 4;
    const rx     = Math.cos(ang) * R * 0.68, ry = Math.sin(ang) * R * 0.68;
    const ribDir = new THREE.Vector3(rx, ry, 0).normalize();
    const rib    = new THREE.Mesh(
      new THREE.CylinderGeometry(0.016 * R, 0.016 * R, R * 0.70, 5), strutMat);
    rib.position.set(rx / 2, ry / 2, -depth + 0.02);
    rib.quaternion.setFromUnitVectors(up, ribDir);
    dG.add(rib);
  }

  // ── Mounting arm (mast → dish back) + pivot block ──
  const stopF = 1 - depth / dist;
  tube(G, 0, cy, 0, cx * stopF * 0.92, cy, cz * stopF * 0.92, 0.055 * R + 0.04, M_STEEL);
  const pivot = new THREE.Mesh(new THREE.BoxGeometry(0.18 * R, 0.28 * R, 0.20 * R), hubMat);
  pivot.rotation.y = faceY;
  pivot.position.set(cx * stopF, cy, cz * stopF);
  G.add(pivot);
}

function buildLattice(G, H, baseR, topR, mat) {
  const legR = Math.max(0.045, baseR * 0.034);
  const ringR = Math.max(0.028, baseR * 0.017);
  const braceR = Math.max(0.022, baseR * 0.014);
  for (let i = 0; i < 4; i++) {
    const a = (i / 4) * Math.PI * 2 + Math.PI / 4;
    tube(G, Math.cos(a)*baseR, 0, Math.sin(a)*baseR, Math.cos(a)*topR, H, Math.sin(a)*topR, legR, mat);
  }
  const levels = [3, 6, 10, 14, 18, 22, 27, 32].filter(h => h < H);
  levels.forEach((h, li) => {
    const r = baseR + (topR - baseR) * (h / H);
    const corners = Array.from({ length: 4 }, (_, i) => {
      const a = (i / 4) * Math.PI * 2 + Math.PI / 4;
      return [Math.cos(a) * r, h, Math.sin(a) * r];
    });
    for (let i = 0; i < 4; i++) {
      const j = (i + 1) % 4;
      tube(G, corners[i][0], corners[i][1], corners[i][2], corners[j][0], corners[j][1], corners[j][2], ringR, mat);
    }
    if (li < levels.length - 1) {
      const h2 = levels[li + 1];
      const r2 = baseR + (topR - baseR) * (h2 / H);
      const next = Array.from({ length: 4 }, (_, i) => {
        const a = (i / 4) * Math.PI * 2 + Math.PI / 4;
        return [Math.cos(a) * r2, h2, Math.sin(a) * r2];
      });
      for (let i = 0; i < 4; i++) {
        const j = (i + 1) % 4;
        tube(G, corners[i][0], corners[i][1], corners[i][2], next[j][0], next[j][1], next[j][2], braceR, mat);
        tube(G, corners[j][0], corners[j][1], corners[j][2], next[i][0], next[i][1], next[i][2], braceR, mat);
      }
    }
  });
}

function makeLabel(text, hexColor) {
  const css = '#' + hexColor.toString(16).padStart(6, '0');
  const cv = document.createElement('canvas');
  cv.width = 256; cv.height = 72;
  const ctx = cv.getContext('2d');
  ctx.fillStyle = 'rgba(0,5,18,0.85)';
  ctx.beginPath(); ctx.roundRect(2, 2, 252, 68, 10); ctx.fill();
  ctx.strokeStyle = css; ctx.lineWidth = 2.5;
  ctx.beginPath(); ctx.roundRect(2, 2, 252, 68, 10); ctx.stroke();
  ctx.fillStyle = css;
  ctx.font = 'bold 30px monospace';
  ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
  ctx.fillText(text, 128, 36);
  const tex = new THREE.CanvasTexture(cv);
  const spr = new THREE.Sprite(new THREE.SpriteMaterial({ map: tex, transparent: true, depthWrite: false }));
  spr.scale.set(12, 3.5, 1);
  return spr;
}

function makeUELabel(text, hexColor) {
  const css = '#' + hexColor.toString(16).padStart(6, '0');
  const cv = document.createElement('canvas');
  cv.width = 128; cv.height = 40;
  const ctx = cv.getContext('2d');
  ctx.fillStyle = 'rgba(0,4,14,0.82)';
  ctx.beginPath(); ctx.roundRect(1, 1, 126, 38, 7); ctx.fill();
  ctx.strokeStyle = css; ctx.lineWidth = 1.5;
  ctx.beginPath(); ctx.roundRect(1, 1, 126, 38, 7); ctx.stroke();
  ctx.fillStyle = css;
  ctx.font = 'bold 20px monospace';
  ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
  ctx.fillText(text, 64, 20);
  const tex = new THREE.CanvasTexture(cv);
  const spr = new THREE.Sprite(new THREE.SpriteMaterial({ map: tex, transparent: true, depthWrite: false }));
  spr.scale.set(4.5, 1.4, 1);
  return spr;
}

function makeHexTexture() {
  const S = 1024;
  const cv = document.createElement('canvas');
  cv.width = cv.height = S;
  const ctx = cv.getContext('2d');
  ctx.fillStyle = '#0e1f3a';
  ctx.fillRect(0, 0, S, S);
  const R = 32;
  const W = R * Math.sqrt(3);
  const H = R * 1.5;
  ctx.lineWidth = 1.3;
  for (let row = -1; row < S / H + 2; row++) {
    for (let col = -1; col < S / W + 2; col++) {
      const cx = col * W + (row % 2) * (W / 2);
      const cy = row * H;
      ctx.beginPath();
      for (let i = 0; i < 6; i++) {
        const a = (i * 60 + 30) * Math.PI / 180;
        i === 0 ? ctx.moveTo(cx + R * Math.cos(a), cy + R * Math.sin(a))
                : ctx.lineTo(cx + R * Math.cos(a), cy + R * Math.sin(a));
      }
      ctx.closePath();
      ctx.fillStyle = 'rgba(0,40,90,0.22)';
      ctx.fill();
      ctx.strokeStyle = 'rgba(0,170,245,0.65)';
      ctx.stroke();
      ctx.fillStyle = 'rgba(0,210,255,0.50)';
      ctx.beginPath(); ctx.arc(cx, cy, 1.5, 0, Math.PI * 2); ctx.fill();
    }
  }
  const tex = new THREE.CanvasTexture(cv);
  tex.wrapS = tex.wrapT = THREE.RepeatWrapping;
  tex.repeat.set(10, 10);
  return tex;
}

// ── LTE macro tower ──────────────────────────────────────────────
function buildTower(scene, cell, ti) {
  const col  = loadHex(_net.cells[ti].load);
  const refs = { leds: [], lights: [], fans: [], halos: [], bMat: null, bLight: null };
  const G    = new THREE.Group();
  G.position.set(cell.x, 0, cell.z);

  const platM = new THREE.MeshStandardMaterial({ color: 0x080e1c, metalness: 0.92, roughness: 0.18 });
  const plat = new THREE.Mesh(new THREE.CylinderGeometry(5.8, 6.4, 0.55, 8), platM);
  plat.position.y = 0.27; G.add(plat);

  const rimM = new THREE.MeshBasicMaterial({ color: col, transparent: true, opacity: 0.7, blending: THREE.AdditiveBlending });
  refs.halos.push(rimM);
  const rim = new THREE.Mesh(new THREE.TorusGeometry(6.2, 0.22, 6, 40), rimM);
  rim.rotation.x = Math.PI / 2; rim.position.y = 0.55; G.add(rim);

  buildLattice(G, 44, 3.8, 0.38, M_STEEL);

  const topD = new THREE.Mesh(new THREE.CylinderGeometry(2.0, 2.0, 0.22, 8), M_STEEL);
  topD.position.y = 44.1; G.add(topD);
  const rod = new THREE.Mesh(new THREE.CylinderGeometry(0.035, 0.035, 5, 5), M_STEEL);
  rod.position.y = 46.6; G.add(rod);

  [41.5, 44.0, 47.0].forEach((h, ri) => {
    const rMat = new THREE.MeshBasicMaterial({
      color: ri === 1 ? col : 0x0077dd, transparent: true,
      opacity: ri === 1 ? 0.6 : 0.35, blending: THREE.AdditiveBlending, side: THREE.DoubleSide,
    });
    if (ri === 1) refs.leds.push(rMat);
    const rMesh = new THREE.Mesh(new THREE.TorusGeometry(1.5 + ri * 0.55, 0.06, 5, 42), rMat);
    rMesh.position.y = h; G.add(rMesh);
    HOLO_RINGS.push({ mesh: rMesh, speed: (0.55 + ri * 0.25) * (ri % 2 === 0 ? 1 : -1), tilt: ri % 2 === 0 ? 'x' : 'z' });
  });

  for (let s = 0; s < 3; s++) {
    const sa = (s / 3) * Math.PI * 2;
    const ledM = new THREE.MeshStandardMaterial({ color: col, emissive: col, emissiveIntensity: 7, transparent: true, opacity: 0.92 });
    refs.leds.push(ledM);
    const panel = new THREE.Mesh(new THREE.BoxGeometry(0.22, 3.6, 0.1), ledM);
    panel.position.set(Math.cos(sa) * 2.0, 43.0, Math.sin(sa) * 2.0);
    panel.rotation.y = -sa; G.add(panel);
  }

  // ── Sector antennas — 3 tall panels at 120° (characteristic of macro towers) ──
  const sectHousMat  = new THREE.MeshStandardMaterial({ color: 0x2c3a4a, metalness: 0.78, roughness: 0.28 });
  const sectFaceMat  = new THREE.MeshStandardMaterial({ color: 0x1a2535, metalness: 0.60, roughness: 0.50 });
  const sectRailMat  = new THREE.MeshStandardMaterial({ color: 0x4a5e72, metalness: 0.88, roughness: 0.18 });
  const sectPortMat  = new THREE.MeshStandardMaterial({ color: 0x0d1520, metalness: 0.50, roughness: 0.65 });
  for (let s = 0; s < 3; s++) {
    const sa = (s / 3) * Math.PI * 2 + Math.PI / 6;
    const sR = 3.6;
    const cx = Math.cos(sa), cz = Math.sin(sa);
    // Main housing body
    const housing = new THREE.Mesh(new THREE.BoxGeometry(1.50, 9.8, 0.52), sectHousMat);
    housing.position.set(cx * sR, 34.0, cz * sR);
    housing.rotation.y = -sa; G.add(housing);
    const face = new THREE.Mesh(new THREE.BoxGeometry(1.18, 8.6, 0.10), sectFaceMat);
    face.position.set(cx * (sR + 0.24), 34.0, cz * (sR + 0.24));
    face.rotation.y = -sa; G.add(face);
    for (const side of [-0.62, 0.62]) {
      const railX = cx * (sR + 0.04) + Math.cos(sa + Math.PI / 2) * side;
      const railZ = cz * (sR + 0.04) + Math.sin(sa + Math.PI / 2) * side;
      const rail = new THREE.Mesh(new THREE.BoxGeometry(0.14, 10.1, 0.56), sectRailMat);
      rail.position.set(railX, 34.0, railZ);
      rail.rotation.y = -sa; G.add(rail);
    }
    for (const by of [29.5, 34.0, 38.5]) {
      const band = new THREE.Mesh(new THREE.BoxGeometry(1.55, 0.18, 0.58), sectRailMat);
      band.position.set(cx * sR, by, cz * sR);
      band.rotation.y = -sa; G.add(band);
    }
    const capM = new THREE.MeshStandardMaterial({ color: 0x3d4f60, metalness: 0.65, roughness: 0.38 });
    const cap = new THREE.Mesh(new THREE.CylinderGeometry(0.0, 0.76, 0.90, 8), capM);
    cap.position.set(cx * sR, 39.35, cz * sR); G.add(cap);
    const capBase = new THREE.Mesh(new THREE.CylinderGeometry(0.76, 0.76, 0.28, 8), sectRailMat);
    capBase.position.set(cx * sR, 38.86, cz * sR); G.add(capBase);
    const port = new THREE.Mesh(new THREE.BoxGeometry(0.80, 0.60, 0.40), sectPortMat);
    port.position.set(cx * sR, 29.28, cz * sR);
    port.rotation.y = -sa; G.add(port);
    for (const side of [-0.18, 0.18]) {
      const pinX = cx * (sR + 0.22) + Math.cos(sa + Math.PI / 2) * side;
      const pinZ = cz * (sR + 0.22) + Math.sin(sa + Math.PI / 2) * side;
      const pin = new THREE.Mesh(new THREE.CylinderGeometry(0.045, 0.045, 0.30, 6), M_STEEL);
      pin.rotation.x = Math.PI / 2; pin.rotation.z = sa;
      pin.position.set(pinX, 29.28, pinZ); G.add(pin);
    }
    const stripM = new THREE.MeshBasicMaterial({ color: col, transparent: true, opacity: 0.72,
      blending: THREE.AdditiveBlending });
    refs.leds.push(stripM);
    const strip = new THREE.Mesh(new THREE.BoxGeometry(0.12, 8.20, 0.08), stripM);
    strip.position.set(cx * (sR + 0.295), 34.0, cz * (sR + 0.295));
    strip.rotation.y = -sa; G.add(strip);
    for (let d = 0; d < 5; d++) {
      const dotM = new THREE.MeshBasicMaterial({ color: col, transparent: true, opacity: 0.90,
        blending: THREE.AdditiveBlending });
      refs.leds.push(dotM);
      const dot = new THREE.Mesh(new THREE.SphereGeometry(0.055, 6, 6), dotM);
      dot.position.set(cx * (sR + 0.30) + Math.cos(sa + Math.PI / 2) * 0.42,
                       30.8 + d * 0.70,
                       cz * (sR + 0.30) + Math.sin(sa + Math.PI / 2) * 0.42);
      G.add(dot);
    }
    for (const by of [36.5, 31.5]) {
      tube(G, 0, by, 0, cx * (sR - 0.76), by, cz * (sR - 0.76), 0.09, M_STEEL);
      const nub = new THREE.Mesh(new THREE.CylinderGeometry(0.16, 0.16, 0.24, 6), sectRailMat);
      nub.position.set(cx * (sR - 0.55), by, cz * (sR - 0.55)); G.add(nub);
    }
  }

  // ── 3 backhaul parabolic dishes (120° apart) ──
  for (let p = 0; p < 3; p++) {
    const pa  = (p / 3) * Math.PI * 2 + Math.PI / 4;
    const pR  = 4.5;
    const pcx = Math.cos(pa) * pR, pcz = Math.sin(pa) * pR;
    buildParaDish(G, pcx, 20.0 + p * 0.80, pcz,
      Math.atan2(Math.cos(pa), Math.sin(pa)), 1.05, refs.leds, col);
  }

  // ── Large microwave-link dish ──
  buildParaDish(G, 3.2, 24.0, 1.6, Math.atan2(3.2, 1.6), 1.38, refs.leds, col);
  tube(G, 0, 22, 0, 2.2, 22, 0.8, 0.065, M_STEEL);

  const bMat = new THREE.MeshStandardMaterial({ color: 0xff2244, emissive: 0xff2244, emissiveIntensity: 12 });
  refs.bMat = bMat;
  const bm = new THREE.Mesh(new THREE.SphereGeometry(0.22, 8, 8), bMat);
  bm.position.y = 48.8; G.add(bm);
  const bLight = new THREE.PointLight(0xff2244, 0, 22);
  bLight.position.y = 48.8; refs.bLight = bLight; G.add(bLight);

  const tl = new THREE.PointLight(col, 7, 90); tl.position.y = 24;
  refs.lights.push(tl); G.add(tl);

  const beamM = new THREE.MeshBasicMaterial({ color: col, transparent: true, opacity: 0.055,
    blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide });
  refs.fans.push(beamM);
  const beam = new THREE.Mesh(new THREE.CylinderGeometry(0.78, 10.4, 155, 16, 1, true), beamM);
  beam.position.set(cell.x, 135, cell.z); scene.add(beam);

  const lbl = makeLabel(cell.id, 0x00aaff);
  lbl.position.set(cell.x, 82, cell.z); scene.add(lbl);
  G.scale.set(1.3, 1.3, 1.3);
  scene.add(G);

  const scanMat = new THREE.MeshBasicMaterial({ color: col, transparent: true, opacity: 0,
    blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide });
  refs.halos.push(scanMat);
  const scanRing = new THREE.Mesh(new THREE.RingGeometry(4.4, 5.8, 48), scanMat);
  scanRing.rotation.x = -Math.PI / 2; scanRing.position.set(cell.x, 1, cell.z);
  scene.add(scanRing);
  SCAN_RINGS.push({ mesh: scanRing, mat: scanMat, minY: 0.5, maxY: 64, speed: 0.18, phase: ti * 0.30 });

  TOWER_REFS[ti] = refs;
}

// ── mmWave small cell ────────────────────────────────────────────
function buildSmallCell(scene, cell, ti) {
  const loadCol = loadHex(_net.cells[ti].load);
  const cellHex = parseInt(cell.color.replace('#', ''), 16);
  const refs = { leds: [], lights: [], fans: [], halos: [], bMat: null, bLight: null };
  const G    = new THREE.Group();
  G.position.set(cell.x, 0, cell.z);

  const platM = new THREE.MeshStandardMaterial({ color: 0x080e1c, metalness: 0.92, roughness: 0.18 });
  const plat = new THREE.Mesh(new THREE.CylinderGeometry(2.6, 3.0, 0.32, 6), platM);
  plat.position.y = 0.16; G.add(plat);

  const rimM = new THREE.MeshBasicMaterial({ color: cellHex, transparent: true, opacity: 0.62, blending: THREE.AdditiveBlending });
  const rim = new THREE.Mesh(new THREE.TorusGeometry(2.8, 0.12, 5, 24), rimM);
  rim.rotation.x = Math.PI / 2; rim.position.y = 0.32; G.add(rim);

  buildLattice(G, 20, 1.6, 0.18, M_STEEL);

  const topD = new THREE.Mesh(new THREE.CylinderGeometry(0.85, 0.85, 0.15, 8), M_STEEL);
  topD.position.y = 20.07; G.add(topD);

  const rMat = new THREE.MeshBasicMaterial({ color: cellHex, transparent: true, opacity: 0.6,
    blending: THREE.AdditiveBlending, side: THREE.DoubleSide });
  const rMesh = new THREE.Mesh(new THREE.TorusGeometry(1.0, 0.05, 5, 30), rMat);
  rMesh.position.y = 20.6; G.add(rMesh);
  HOLO_RINGS.push({ mesh: rMesh, speed: 0.75, tilt: 'x' });

  const rMat2 = new THREE.MeshBasicMaterial({ color: cellHex, transparent: true, opacity: 0.35,
    blending: THREE.AdditiveBlending, side: THREE.DoubleSide });
  const rMesh2 = new THREE.Mesh(new THREE.TorusGeometry(1.3, 0.04, 5, 30), rMat2);
  rMesh2.position.y = 20.6; G.add(rMesh2);
  HOLO_RINGS.push({ mesh: rMesh2, speed: -0.5, tilt: 'z' });

  for (let s = 0; s < 2; s++) {
    const sa = (s / 2) * Math.PI * 2;
    const faceM = new THREE.MeshStandardMaterial({ color: cellHex, emissive: cellHex, emissiveIntensity: 5, transparent: true, opacity: 0.9 });
    const face = new THREE.Mesh(new THREE.BoxGeometry(0.28, 2.4, 0.1), faceM);
    face.position.set(Math.cos(sa) * 1.0, 19.5, Math.sin(sa) * 1.0); face.rotation.y = -sa; G.add(face);
    const ledM = new THREE.MeshStandardMaterial({ color: loadCol, emissive: loadCol, emissiveIntensity: 9, transparent: true, opacity: 0.9 });
    refs.leds.push(ledM);
    const led = new THREE.Mesh(new THREE.BoxGeometry(0.06, 1.8, 0.1), ledM);
    led.position.set(Math.cos(sa) * 1.11, 19.5, Math.sin(sa) * 1.11); led.rotation.y = -sa; G.add(led);
  }

  // ── 3 Compact 5G auxiliary panels at 120° — upright, square, properly bracketed ──
  const scBodyMat  = new THREE.MeshStandardMaterial({ color: 0x18273a, metalness: 0.78, roughness: 0.28 });
  const scInsetMat = new THREE.MeshStandardMaterial({ color: 0x0a1420, metalness: 0.55, roughness: 0.50 });
  const scFrameMat = new THREE.MeshStandardMaterial({ color: 0x3a5570, metalness: 0.90, roughness: 0.16 });
  const scArmMat   = new THREE.MeshStandardMaterial({ color: 0x6a7e90, metalness: 0.88, roughness: 0.22 });

  for (let s = 0; s < 3; s++) {
    const sa = (s / 3) * Math.PI * 2 + Math.PI / 6;
    const sR = 2.20;
    const cx = Math.cos(sa), cz = Math.sin(sa);

    // ── Mounting arm: horizontal bar out from mast, then vertical clamp ──
    // Horizontal arm from mast surface to panel back
    tube(G, cx * 0.25, 6.5, cz * 0.25, cx * (sR - 0.18), 6.5, cz * (sR - 0.18), 0.075, M_STEEL);
    // Vertical clamp bar (holds panel at top and bottom)
    tube(G, cx * (sR - 0.10), 5.6, cz * (sR - 0.10),
            cx * (sR - 0.10), 7.5, cz * (sR - 0.10), 0.060, M_STEEL);
    // Clamp collar top
    const clampT = new THREE.Mesh(new THREE.CylinderGeometry(0.14, 0.14, 0.16, 8), scArmMat);
    clampT.position.set(cx * (sR - 0.10), 7.42, cz * (sR - 0.10)); G.add(clampT);
    // Clamp collar bottom
    const clampB = new THREE.Mesh(new THREE.CylinderGeometry(0.14, 0.14, 0.16, 8), scArmMat);
    clampB.position.set(cx * (sR - 0.10), 5.68, cz * (sR - 0.10)); G.add(clampB);

    // ── Panel itself — square, upright, facing outward ──
    const pG = new THREE.Group();
    pG.position.set(cx * sR, 6.5, cz * sR);
    pG.rotation.y = -sa; // face directly outward, perfectly vertical
    G.add(pG);

    // Main body (square-ish panel, like Nokia AirScale)
    pG.add(new THREE.Mesh(new THREE.BoxGeometry(1.30, 1.50, 0.26), scBodyMat));
    // Frame (4 edges)
    [[-0.57, 0, 0.75], [0.57, 0, 0.75], [0, 0.68, 0.75], [0, -0.68, 0.75]].forEach(([fx, fy, fz], fi) => {
      const isH = fi >= 2;
      const frm = new THREE.Mesh(
        new THREE.BoxGeometry(isH ? 1.34 : 0.12, isH ? 0.10 : 1.52, 0.08), scFrameMat);
      frm.position.set(fx, fy, fz); pG.add(frm);
    });
    // Recessed inner face
    const inset = new THREE.Mesh(new THREE.BoxGeometry(1.08, 1.28, 0.06), scInsetMat);
    inset.position.z = 0.14; pG.add(inset);
    // Corner bolt accents (4)
    for (const [bx, by] of [[-0.50, 0.58], [0.50, 0.58], [-0.50, -0.58], [0.50, -0.58]]) {
      const bolt = new THREE.Mesh(new THREE.CylinderGeometry(0.055, 0.055, 0.12, 6), scFrameMat);
      bolt.rotation.x = Math.PI / 2; bolt.position.set(bx, by, 0.18); pG.add(bolt);
    }
    // Antenna element grid: 3 × 3 (compact array on face)
    for (let row = 0; row < 3; row++) {
      for (let col2 = 0; col2 < 3; col2++) {
        const eM = new THREE.MeshStandardMaterial({ color: cellHex, emissive: cellHex,
          emissiveIntensity: 4.0, transparent: true, opacity: 0.92 });
        refs.leds.push(eM);
        const el = new THREE.Mesh(new THREE.CylinderGeometry(0.072, 0.072, 0.09, 8), eM);
        el.rotation.x = Math.PI / 2;
        el.position.set(-0.32 + col2 * 0.32, -0.28 + row * 0.28, 0.22); pG.add(el);
      }
    }
    // Horizontal glow line across middle of face
    const glM = new THREE.MeshBasicMaterial({ color: cellHex, transparent: true, opacity: 0.55,
      blending: THREE.AdditiveBlending });
    refs.leds.push(glM);
    const glowLine = new THREE.Mesh(new THREE.BoxGeometry(0.92, 0.045, 0.06), glM);
    glowLine.position.set(0, 0, 0.17); pG.add(glowLine);
    // Status LED dot (load color) — bottom-left corner
    const ldM = new THREE.MeshBasicMaterial({ color: loadCol, transparent: true, opacity: 0.95,
      blending: THREE.AdditiveBlending });
    refs.leds.push(ldM);
    const ld = new THREE.Mesh(new THREE.SphereGeometry(0.045, 6, 6), ldM);
    ld.position.set(-0.48, -0.60, 0.18); pG.add(ld);
    // Cable exit at bottom
    tube(G, cx * (sR - 0.02), 5.62, cz * (sR - 0.02),
            cx * (sR - 0.02), 5.22, cz * (sR - 0.02), 0.040, M_STEEL);
  }

  // ── Horn antennas — 2 directional mmWave backhaul horns at upper mast ──
  const hornBodyMat = new THREE.MeshStandardMaterial({ color: 0x8899aa, metalness: 0.90, roughness: 0.14 });
  const hornRimMat  = new THREE.MeshStandardMaterial({ color: 0xaabbcc, metalness: 0.92, roughness: 0.10 });
  const hornNubMat  = new THREE.MeshStandardMaterial({ color: 0x3a5068, metalness: 0.88, roughness: 0.18 });
  [
    { x:  1.30, y: 18.0, z: 0, ry:  Math.PI / 2, rz:  0.18 },
    { x: -1.30, y: 17.5, z: 0, ry: -Math.PI / 2, rz: -0.18 },
  ].forEach(hc => {
    const hG = new THREE.Group();
    hG.position.set(hc.x, hc.y, hc.z);
    hG.rotation.y = hc.ry;
    hG.rotation.z = hc.rz; // slight upward tilt
    G.add(hG);
    // Waveguide body (cylinder along local +z)
    const wg = new THREE.Mesh(new THREE.CylinderGeometry(0.115, 0.115, 0.52, 8), hornBodyMat);
    wg.rotation.x = Math.PI / 2; wg.position.z = 0.26; hG.add(wg);
    // Back cap (hemisphere)
    const bCap = new THREE.Mesh(
      new THREE.SphereGeometry(0.115, 8, 6, 0, Math.PI * 2, 0, Math.PI / 2), hornBodyMat);
    bCap.rotation.x = -Math.PI / 2; hG.add(bCap);
    // Flare cone
    const flare = new THREE.Mesh(
      new THREE.CylinderGeometry(0.36, 0.115, 0.44, 10, 1, true), hornBodyMat);
    flare.rotation.x = Math.PI / 2; flare.position.z = 0.74; hG.add(flare);
    // Aperture rim ring
    const aRim = new THREE.Mesh(new THREE.TorusGeometry(0.36, 0.028, 6, 20), hornRimMat);
    aRim.rotation.x = Math.PI / 2; aRim.position.z = 0.96; hG.add(aRim);
    // Feed probe glow (active element indicator)
    const probeM = new THREE.MeshBasicMaterial({ color: cellHex, transparent: true, opacity: 0.78,
      blending: THREE.AdditiveBlending });
    refs.leds.push(probeM);
    const probe = new THREE.Mesh(new THREE.SphereGeometry(0.055, 6, 5), probeM);
    probe.position.z = 0.26; hG.add(probe);
    // Mounting bracket to mast
    tube(G, 0, hc.y, 0, hc.x * 0.75, hc.y, 0, 0.055, M_STEEL);
    const hNub = new THREE.Mesh(new THREE.CylinderGeometry(0.10, 0.10, 0.14, 6), hornNubMat);
    hNub.position.set(hc.x * 0.82, hc.y, 0); G.add(hNub);
  });

  // ── Flat MIMO array panel (defining feature of mmWave nodes) ──
  const mimoBgMat   = new THREE.MeshStandardMaterial({ color: 0x0e1722, metalness: 0.60, roughness: 0.42 });
  const mimoFrameMat = new THREE.MeshStandardMaterial({ color: 0x3e5066, metalness: 0.90, roughness: 0.16 });
  const mimoInsetMat = new THREE.MeshStandardMaterial({ color: 0x141e2c, metalness: 0.50, roughness: 0.55 });
  // Main panel body
  const mimoPanel = new THREE.Mesh(new THREE.BoxGeometry(5.6, 3.2, 0.44), mimoBgMat);
  mimoPanel.position.y = 21.0; G.add(mimoPanel);
  // Outer frame (thick rails — top, bottom, left, right)
  [{ w: 5.80, h: 0.22, x: 0, y: 22.71 }, { w: 5.80, h: 0.22, x: 0, y: 19.29 }].forEach(r => {
    const rail = new THREE.Mesh(new THREE.BoxGeometry(r.w, r.h, 0.52), mimoFrameMat);
    rail.position.set(r.x, r.y, 0); G.add(rail);
  });
  [{ w: 0.22, h: 3.42, x: -2.90, y: 21.0 }, { w: 0.22, h: 3.42, x: 2.90, y: 21.0 }].forEach(r => {
    const rail = new THREE.Mesh(new THREE.BoxGeometry(r.w, r.h, 0.52), mimoFrameMat);
    rail.position.set(r.x, r.y, 0); G.add(rail);
  });
  // Corner bolt accents
  for (const [bx, by] of [[-2.72, 22.60], [2.72, 22.60], [-2.72, 19.40], [2.72, 19.40]]) {
    const bolt = new THREE.Mesh(new THREE.CylinderGeometry(0.10, 0.10, 0.18, 6), mimoFrameMat);
    bolt.rotation.x = Math.PI / 2; bolt.position.set(bx, by, 0.28); G.add(bolt);
  }
  // Inset sub-panel (dark recess inside frame)
  const inset = new THREE.Mesh(new THREE.BoxGeometry(5.20, 2.80, 0.10), mimoInsetMat);
  inset.position.set(0, 21.0, 0.22); G.add(inset);
  // Vertical divider channels (3 dividers = 4 sub-zones)
  for (const dx of [-1.30, 0, 1.30]) {
    const div = new THREE.Mesh(new THREE.BoxGeometry(0.10, 2.80, 0.14), mimoFrameMat);
    div.position.set(dx, 21.0, 0.28); G.add(div);
  }
  // Antenna element grid: 4 rows × 8 columns
  const ROWS = 4, COLS = 8;
  const gW = 4.60, gH = 2.30;
  for (let row = 0; row < ROWS; row++) {
    for (let col2 = 0; col2 < COLS; col2++) {
      const eMat = new THREE.MeshStandardMaterial({ color: cellHex, emissive: cellHex,
        emissiveIntensity: 4.5, transparent: true, opacity: 0.94 });
      refs.leds.push(eMat);
      const elem = new THREE.Mesh(new THREE.CylinderGeometry(0.075, 0.075, 0.10, 8), eMat);
      elem.rotation.x = Math.PI / 2;
      const ex = -gW / 2 + (col2 + 0.5) * (gW / COLS);
      const ey = 19.85 + (row + 0.5) * (gH / ROWS);
      elem.position.set(ex, ey, 0.30); G.add(elem);
    }
  }
  // Side heat-fin details (left + right edge)
  for (const sx of [-2.96, 2.96]) {
    for (let f = 0; f < 5; f++) {
      const fin = new THREE.Mesh(new THREE.BoxGeometry(0.18, 0.38, 0.52), mimoFrameMat);
      fin.position.set(sx, 19.75 + f * 0.56, 0); G.add(fin);
    }
  }
  // Mounting arm from mast to panel back
  tube(G, 0, 21.0, 0, 0, 21.0, -0.26, 0.14, M_STEEL);
  const mountBlock = new THREE.Mesh(new THREE.BoxGeometry(0.40, 0.70, 0.38), mimoFrameMat);
  mountBlock.position.set(0, 21.0, -0.44); G.add(mountBlock);

  // ── 2 parabolic backhaul dishes on opposite sides below MIMO panel ──
  buildParaDish(G,  3.2, 15.0, 0,  Math.PI / 2, 0.82, refs.leds, cellHex);
  buildParaDish(G, -3.2, 14.2, 0, -Math.PI / 2, 0.82, refs.leds, cellHex);

  const bMat = new THREE.MeshStandardMaterial({ color: 0xff2244, emissive: 0xff2244, emissiveIntensity: 14 });
  refs.bMat = bMat;
  const bm = new THREE.Mesh(new THREE.SphereGeometry(0.14, 8, 8), bMat);
  bm.position.y = 22.0; G.add(bm);
  const bLight = new THREE.PointLight(0xff2244, 0, 15);
  bLight.position.y = 22.0; refs.bLight = bLight; G.add(bLight);

  const tl = new THREE.PointLight(loadCol, 5, 52); tl.position.y = 8;
  refs.lights.push(tl); G.add(tl);

  const beamM = new THREE.MeshBasicMaterial({ color: cellHex, transparent: true, opacity: 0.08,
    blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide });
  const beam = new THREE.Mesh(new THREE.CylinderGeometry(0.42, 5.25, 90, 12, 1, true), beamM);
  beam.position.set(cell.x, 72, cell.z); scene.add(beam);

  const lbl = makeLabel(cell.id, cellHex);
  lbl.position.set(cell.x, 42, cell.z); scene.add(lbl);
  G.scale.set(1.5, 1.5, 1.5);
  scene.add(G);

  // Scan ring — rises from base to top
  const scanMat = new THREE.MeshBasicMaterial({ color: loadCol, transparent: true, opacity: 0,
    blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide });
  refs.halos.push(scanMat);
  const scanRing = new THREE.Mesh(new THREE.RingGeometry(1.9, 2.8, 36), scanMat);
  scanRing.rotation.x = -Math.PI / 2; scanRing.position.set(cell.x, 0.2, cell.z);
  scene.add(scanRing);
  SCAN_RINGS.push({ mesh: scanRing, mat: scanMat, minY: 0.2, maxY: 34, speed: 0.30, phase: ti * 0.44 });

  TOWER_REFS[ti] = refs;
}

// ── Init / reinit ────────────────────────────────────────────────
export function initScene(net, cfg, opts = {}) {
  _net = net;

  if (_animId !== null) { cancelAnimationFrame(_animId); _animId = null; }
  TOWER_REFS.length       = 0;
  UE_GROUPS.length        = 0;
  HOLO_RINGS.length       = 0;
  HANDOVER_FLASHES.length = 0;
  GROUND_CONNS.length     = 0;
  SCAN_RINGS.length       = 0;
  ARC_PACKETS.length      = 0;
  COVERAGE_RINGS.length   = 0;
  _scene     = null;
  _ueTargets = null;

  const canvas = document.getElementById('cv');
  if (!canvas) throw new Error('Canvas #cv not found');

  if (!_renderer) {
    _renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
    _renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
    _renderer.shadowMap.enabled = true;
    _renderer.shadowMap.type    = THREE.PCFSoftShadowMap;
    _renderer.toneMapping       = THREE.ACESFilmicToneMapping;
    _renderer.toneMappingExposure = 0.82;
    const _doResize = () => {
      _renderer.setSize(innerWidth, innerHeight);
      if (_composer) _composer.setSize(innerWidth, innerHeight);
      if (_camera) { _camera.aspect = innerWidth / innerHeight; _camera.updateProjectionMatrix(); }
    };
    window.addEventListener('resize', _doResize);
    window.addEventListener('load',   _doResize);
  }
  _renderer.setSize(innerWidth, innerHeight);

  const scene = new THREE.Scene();
  _scene = scene;
  scene.background = new THREE.Color(0x0d1e35);
  scene.fog = new THREE.FogExp2(0x0d1e35, 0.00030);

  const camera = new THREE.PerspectiveCamera(52, innerWidth / innerHeight, 0.1, 1400);
  _camera = camera;
  const target = new THREE.Vector3(0, 10, 0);
  let theta = 0.25, phi = 0.36, radius = cfg.CELLS.length > 4 ? 220 : 140;
  let drag = false, ox = 0, oy = 0;

  canvas.addEventListener('mousedown', e => { drag = true; ox = e.clientX; oy = e.clientY; });
  window.addEventListener('mouseup',   () => { drag = false; });
  window.addEventListener('mousemove', e => {
    if (!drag) return;
    theta -= (e.clientX - ox) * 0.004;
    phi    = Math.max(0.06, Math.min(1.12, phi - (e.clientY - oy) * 0.004));
    ox = e.clientX; oy = e.clientY;
  });
  canvas.addEventListener('wheel', e => {
    radius = Math.max(24, Math.min(220, radius + e.deltaY * 0.06));
  }, { passive: true });

  // Track press position so click listener can reject drag gestures
  let _pxDown = 0, _pyDown = 0;
  canvas.addEventListener('mousedown', e => { _pxDown = e.clientX; _pyDown = e.clientY; });

  // ── Original lighting ──
  scene.add(new THREE.AmbientLight(0x162840, 9));
  const sun = new THREE.DirectionalLight(0x3355cc, 3.2);
  sun.position.set(60, 100, 45); sun.castShadow = true;
  sun.shadow.mapSize.width = sun.shadow.mapSize.height = 1024;
  sun.shadow.camera.left = sun.shadow.camera.bottom = -250;
  sun.shadow.camera.right = sun.shadow.camera.top = 250;
  sun.shadow.camera.far = 600;
  scene.add(sun);
  const fill = new THREE.DirectionalLight(0x1133aa, 1.8);
  fill.position.set(-60, 50, -40); scene.add(fill);
  scene.add(new THREE.HemisphereLight(0x142040, 0x06121e, 6));

  // ── Ground — original settings ──
  const hexTex = makeHexTexture();
  const gnd = new THREE.Mesh(
    new THREE.PlaneGeometry(800, 800),
    new THREE.MeshBasicMaterial({ map: hexTex })
  );
  gnd.rotation.x = -Math.PI / 2; scene.add(gnd);

  // Subtle radial glow at network center — adds depth without tower blobs
  const radTex = (() => {
    const cv2 = document.createElement('canvas'); cv2.width = cv2.height = 512;
    const cx2 = cv2.getContext('2d');
    const gr = cx2.createRadialGradient(256, 256, 0, 256, 256, 256);
    gr.addColorStop(0,   'rgba(0,90,180,0.22)');
    gr.addColorStop(0.4, 'rgba(0,55,120,0.10)');
    gr.addColorStop(1,   'rgba(0,0,0,0)');
    cx2.fillStyle = gr; cx2.fillRect(0, 0, 512, 512);
    return new THREE.CanvasTexture(cv2);
  })();
  const gndGlow = new THREE.Mesh(new THREE.PlaneGeometry(560, 560),
    new THREE.MeshBasicMaterial({ map: radTex, transparent: true,
      blending: THREE.AdditiveBlending, depthWrite: false }));
  gndGlow.rotation.x = -Math.PI / 2; gndGlow.position.y = 0.06; scene.add(gndGlow);

  // ── Tech grid overlay — spatial reference ──
  {
    const GHALF = 400, GSTEP = 20;
    const gPts = [];
    for (let p = -GHALF; p <= GHALF; p += GSTEP) {
      gPts.push(p, 0.18, -GHALF, p, 0.18, GHALF);
      gPts.push(-GHALF, 0.18, p, GHALF, 0.18, p);
    }
    const gGeo = new THREE.BufferGeometry();
    gGeo.setAttribute('position', new THREE.Float32BufferAttribute(gPts, 3));
    scene.add(new THREE.LineSegments(gGeo, new THREE.LineBasicMaterial({
      color: 0x003d5c, transparent: true, opacity: 0.20,
      blending: THREE.AdditiveBlending, depthWrite: false,
    })));
    // Major lines every 80 units — brighter
    const mPts = [];
    for (let p = -GHALF; p <= GHALF; p += 80) {
      mPts.push(p, 0.20, -GHALF, p, 0.20, GHALF);
      mPts.push(-GHALF, 0.20, p, GHALF, 0.20, p);
    }
    const mGeo = new THREE.BufferGeometry();
    mGeo.setAttribute('position', new THREE.Float32BufferAttribute(mPts, 3));
    scene.add(new THREE.LineSegments(mGeo, new THREE.LineBasicMaterial({
      color: 0x006699, transparent: true, opacity: 0.36,
      blending: THREE.AdditiveBlending, depthWrite: false,
    })));
    // Origin crosshair
    const xhPts = [-18, 0.22, 0, 18, 0.22, 0, 0, 0.22, -18, 0, 0.22, 18];
    const xhGeo = new THREE.BufferGeometry();
    xhGeo.setAttribute('position', new THREE.Float32BufferAttribute(xhPts, 3));
    scene.add(new THREE.LineSegments(xhGeo, new THREE.LineBasicMaterial({
      color: 0x00aaff, transparent: true, opacity: 0.55,
      blending: THREE.AdditiveBlending, depthWrite: false,
    })));
  }

  // ── Towers ──
  cfg.CELLS.forEach((cell, ti) => {
    if (cell.type === 'mmwave') buildSmallCell(scene, cell, ti);
    else                        buildTower(scene, cell, ti);
  });

  // ── Click-to-inspect: invisible pick colliders per cell ──
  const _pickSpheres = [];
  cfg.CELLS.forEach((cell, ti) => {
    const sp = new THREE.Mesh(
      new THREE.SphereGeometry(cell.type === 'lte' ? 10 : 7, 8, 8),
      new THREE.MeshBasicMaterial({ visible: false })
    );
    sp.position.set(cell.x, cell.type === 'lte' ? 25 : 10, cell.z);
    sp.userData.cellIndex = ti;
    scene.add(sp);
    _pickSpheres.push(sp);
  });

  const _raycaster = new THREE.Raycaster();
  const _mouse = new THREE.Vector2();
  canvas.addEventListener('click', e => {
    if (Math.hypot(e.clientX - _pxDown, e.clientY - _pyDown) > 6) return;
    const rect = canvas.getBoundingClientRect();
    _mouse.x = ((e.clientX - rect.left) / rect.width) * 2 - 1;
    _mouse.y = -((e.clientY - rect.top) / rect.height) * 2 + 1;
    _raycaster.setFromCamera(_mouse, camera);
    const hits = _raycaster.intersectObjects(_pickSpheres);
    if (hits.length > 0 && opts.onCellPick) {
      opts.onCellPick(hits[0].object.userData.cellIndex);
    } else if (hits.length === 0 && opts.onCellDismiss) {
      opts.onCellDismiss();
    }
  });

  // ── Coverage footprints ──
  cfg.CELLS.forEach((cell, ti) => {
    const isLTE = cell.type === 'lte';
    const col   = parseInt((cell.color || '#0066cc').replace('#', ''), 16);
    const OR    = isLTE ? 105 : 28;
    const IR    = isLTE ?  60 : 14;
    const ph    = ti * 0.74;
    const spd   = 0.35 + ti * 0.06;

    // Filled coverage zone
    const fillM = new THREE.MeshBasicMaterial({ color: col, transparent: true,
      opacity: isLTE ? 0.055 : 0.072, blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide });
    const fillC = new THREE.Mesh(new THREE.CircleGeometry(OR, 80), fillM);
    fillC.rotation.x = -Math.PI / 2; fillC.position.set(cell.x, 0.09, cell.z);
    scene.add(fillC);
    COVERAGE_RINGS.push({ mat: fillM, base: isLTE ? 0.055 : 0.072, phase: ph + 1.2, speed: spd * 0.6 });

    // Inner ring (signal strength contour)
    const inM = new THREE.MeshBasicMaterial({ color: col, transparent: true,
      opacity: isLTE ? 0.42 : 0.38, blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide });
    const inR = new THREE.Mesh(new THREE.RingGeometry(IR - 0.7, IR + 0.7, 72), inM);
    inR.rotation.x = -Math.PI / 2; inR.position.set(cell.x, 0.11, cell.z);
    scene.add(inR);
    COVERAGE_RINGS.push({ mat: inM,  base: isLTE ? 0.42 : 0.38, phase: ph + 0.5, speed: spd });

    // Outer boundary ring
    const outM = new THREE.MeshBasicMaterial({ color: col, transparent: true,
      opacity: isLTE ? 0.65 : 0.55, blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide });
    const outR = new THREE.Mesh(new THREE.RingGeometry(OR - 0.9, OR + 0.9, 80), outM);
    outR.rotation.x = -Math.PI / 2; outR.position.set(cell.x, 0.12, cell.z);
    scene.add(outR);
    COVERAGE_RINGS.push({ mat: outM, base: isLTE ? 0.65 : 0.55, phase: ph, speed: spd });
  });

  // ── Backhaul arcs + data packets flying through the sky ──
  for (let i = 0; i < cfg.CELLS.length - 1; i++) {
    for (let j = i + 1; j < cfg.CELLS.length; j++) {
      const a = cfg.CELLS[i], b = cfg.CELLS[j];
      const dist = Math.hypot(a.x - b.x, a.z - b.z);
      if (dist > 300) continue;
      const peak = Math.max(50, dist * 0.90);
      const aY = a.type === 'lte' ? 57 : 30;
      const bY = b.type === 'lte' ? 57 : 30;
      const curve = new THREE.QuadraticBezierCurve3(
        new THREE.Vector3(a.x, aY, a.z),
        new THREE.Vector3((a.x + b.x) / 2, peak, (a.z + b.z) / 2),
        new THREE.Vector3(b.x, bY, b.z)
      );
      scene.add(new THREE.Mesh(new THREE.TubeGeometry(curve, 18, 0.10, 5),
        new THREE.MeshBasicMaterial({ color: 0x002266, transparent: true, opacity: 0.40,
          blending: THREE.AdditiveBlending, depthWrite: false })));
      scene.add(new THREE.Mesh(new THREE.TubeGeometry(curve, 18, 0.032, 4),
        new THREE.MeshBasicMaterial({ color: 0x44aaff, transparent: true, opacity: 0.72,
          blending: THREE.AdditiveBlending, depthWrite: false })));
      for (let d = 0; d < 2; d++) {
        const pMat = new THREE.MeshBasicMaterial({ color: 0xffffff, transparent: true, opacity: 0,
          blending: THREE.AdditiveBlending, depthWrite: false });
        const pMesh = new THREE.Mesh(new THREE.SphereGeometry(0.36, 6, 5), pMat);
        scene.add(pMesh);
        ARC_PACKETS.push({
          mesh: pMesh, mat: pMat, curve,
          phase: d * 0.5,
          speed: 0.10 + ((i + j) % 3) * 0.03,
          forward: d === 0, tiA: d === 0 ? i : j,
        });
      }
    }
  }

  // ── UEs ──
  const UE_COLORS = [0x00ff88, 0x00ccff, 0xff6600, 0xffcc00, 0xbb44ff, 0xff2299, 0x44ffcc, 0xff8844];
  let ueIdx = 0;
  cfg.CELLS.forEach((cell, ti) => {
    const numUes   = net.cells[ti]?.ues ?? 3;
    const towerTop = cell.type === 'lte' ? 57 : 30;
    for (let j = 0; j < numUes; j++) {
      const uc         = UE_COLORS[ueIdx % UE_COLORS.length];
      const orbitR     = 7 + (j % 5) * 3.2;
      const orbitSpeed = (0.16 + (j % 6) * 0.038) * (j % 2 === 0 ? 1 : -1);
      const initAngle  = (j / numUes) * Math.PI * 2 + ti * 0.9;
      const uG = new THREE.Group();
      const ix = cell.x + Math.cos(initAngle) * orbitR;
      const iz = cell.z + Math.sin(initAngle) * orbitR;
      uG.position.set(ix, 1.8, iz);
      const coreSize = 0.55 + (ti % 3) * 0.08;
      const initCol = parseInt((cell.color || '#00ccff').replace('#',''), 16);
      const coreMat = new THREE.MeshStandardMaterial({ color: initCol, emissive: initCol,
        emissiveIntensity: 11, transparent: true, opacity: 0.9, metalness: 0.5, roughness: 0.2 });
      uG.add(new THREE.Mesh(new THREE.IcosahedronGeometry(coreSize, 0), coreMat));
      const wireMat = new THREE.MeshBasicMaterial({ color: initCol, wireframe: true,
        transparent: true, opacity: 0.28, blending: THREE.AdditiveBlending });
      uG.add(new THREE.Mesh(new THREE.IcosahedronGeometry(coreSize * 1.65, 0), wireMat));
      const glowSpr = new THREE.Sprite(new THREE.SpriteMaterial({
        color: initCol, transparent: true, opacity: 0.20, blending: THREE.AdditiveBlending, depthWrite: false }));
      glowSpr.scale.set(coreSize * 5, coreSize * 5, 1); uG.add(glowSpr);
      const labelSpr = makeUELabel(`UE ${ueIdx + 1}`, uc);
      labelSpr.position.set(0, coreSize * 3.5 + 0.9, 0); uG.add(labelSpr);
      scene.add(uG);
      // Faint orbital path ring
      const oRM = new THREE.MeshBasicMaterial({ color: uc, transparent: true, opacity: 0.06,
        blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide });
      const oRing = new THREE.Mesh(new THREE.RingGeometry(orbitR - 0.22, orbitR + 0.22, 56), oRM);
      oRing.rotation.x = -Math.PI / 2; oRing.position.set(cell.x, 1.55, cell.z);
      scene.add(oRing);
      const beamPts = [new THREE.Vector3(cell.x, towerTop, cell.z), new THREE.Vector3(ix, 1.8, iz)];
      const beamGeo = new THREE.BufferGeometry().setFromPoints(beamPts);
      const beamMat = new THREE.LineBasicMaterial({ color: cell.color ? parseInt(cell.color.replace('#',''),16) : uc,
        transparent: true, opacity: 0.45, blending: THREE.AdditiveBlending, depthWrite: false });
      scene.add(new THREE.Line(beamGeo, beamMat));
      UE_GROUPS.push({ g: uG, labelSpr, beamGeo, beamMat, coreMat, wireMat,
        glowMat: glowSpr.material,
        towerX: cell.x, towerY: towerTop, towerZ: cell.z,
        orbitR, orbitSpeed, initAngle, baseY: 1.8,
        wanderX: (Math.random() - 0.5) * 180,
        wanderZ: (Math.random() - 0.5) * 180 });
      ueIdx++;
    }
  });

  // ── Stars ──
  {
    const buf = new Float32Array(800 * 3);
    for (let i = 0; i < 800; i++) {
      buf[i*3]   = (Math.random() - 0.5) * 900;
      buf[i*3+1] = 30 + Math.random() * 180;
      buf[i*3+2] = (Math.random() - 0.5) * 900;
    }
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(buf, 3));
    scene.add(new THREE.Points(geo, new THREE.PointsMaterial({
      color: 0x99aadd, size: 0.35, transparent: true, opacity: 0.65, sizeAttenuation: true })));
    const cb = new Float32Array(80 * 3);
    for (let i = 0; i < 80; i++) {
      cb[i*3]   = (Math.random() - 0.5) * 400;
      cb[i*3+1] = 50 + Math.random() * 100;
      cb[i*3+2] = (Math.random() - 0.5) * 400;
    }
    const cg = new THREE.BufferGeometry();
    cg.setAttribute('position', new THREE.BufferAttribute(cb, 3));
    scene.add(new THREE.Points(cg, new THREE.PointsMaterial({
      color: 0xeeeeff, size: 0.7, transparent: true, opacity: 0.8, sizeAttenuation: true })));
  }

  // ── Atmospheric data particles ──
  const ATMO_N = 160;
  const aPos   = new Float32Array(ATMO_N * 3);
  const aInitY = new Float32Array(ATMO_N);
  const aPhase = new Float32Array(ATMO_N);
  const aRad   = new Float32Array(ATMO_N);
  const aAng   = new Float32Array(ATMO_N);
  const aSpd   = new Float32Array(ATMO_N);
  for (let i = 0; i < ATMO_N; i++) {
    const r = 10 + Math.random() * 90;
    const a = Math.random() * Math.PI * 2;
    aPos[i*3]   = Math.cos(a) * r;
    aPos[i*3+1] = 3 + Math.random() * 42;
    aPos[i*3+2] = Math.sin(a) * r;
    aInitY[i] = aPos[i*3+1];
    aPhase[i] = Math.random() * Math.PI * 2;
    aRad[i]   = r;
    aAng[i]   = a;
    aSpd[i]   = 0.007 + Math.random() * 0.013;
  }
  const atmoGeo = new THREE.BufferGeometry();
  atmoGeo.setAttribute('position', new THREE.BufferAttribute(aPos, 3));
  scene.add(new THREE.Points(atmoGeo, new THREE.PointsMaterial({
    color: 0x1a55dd, size: 0.20, transparent: true, opacity: 0.48,
    sizeAttenuation: true, blending: THREE.AdditiveBlending, depthWrite: false })));

  // ── Bloom — original settings ──
  _composer = new EffectComposer(_renderer);
  _composer.addPass(new RenderPass(scene, camera));
  _composer.addPass(new UnrealBloomPass(
    new THREE.Vector2(innerWidth, innerHeight),
    0.72, 0.44, 0.30
  ));

  // ── Horizon rings — original ──
  [216, 252].forEach((r, i) => {
    const mesh = new THREE.Mesh(
      new THREE.RingGeometry(r - 0.6, r + 0.6, 200),
      new THREE.MeshBasicMaterial({ color: [0x0088dd, 0x004488][i], transparent: true,
        opacity: [0.48, 0.20][i], blending: THREE.AdditiveBlending, depthWrite: false })
    );
    mesh.rotation.x = -Math.PI / 2; mesh.position.y = 0.3 + i * 0.1;
    scene.add(mesh);
  });

  // ── Radar sweep — sector style ──
  const RADAR_R = 210;
  const radarGroup = new THREE.Group();
  radarGroup.position.y = 0.20; scene.add(radarGroup);
  // Fading trail sectors
  for (let i = 0; i < 10; i++) {
    const fade = Math.pow(1 - i / 10, 2.4) * 0.048;
    const secGeo = new THREE.CircleGeometry(RADAR_R, 56, -i * 0.095, 0.095);
    const secMesh = new THREE.Mesh(secGeo, new THREE.MeshBasicMaterial({
      color: 0x00ffaa, transparent: true, opacity: fade,
      blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide }));
    secMesh.rotation.x = -Math.PI / 2; radarGroup.add(secMesh);
  }
  // Bright leading edge line
  const edgeMesh = new THREE.Mesh(new THREE.PlaneGeometry(RADAR_R, 0.7),
    new THREE.MeshBasicMaterial({ color: 0x00ffcc, transparent: true, opacity: 0.62,
      blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide }));
  edgeMesh.rotation.x = -Math.PI / 2; edgeMesh.position.set(RADAR_R / 2, 0.12, 0);
  radarGroup.add(edgeMesh);

  // ── Ground connection lines ──
  const GND_Y = 0.30;
  for (let i = 0; i < cfg.CELLS.length; i++) {
    for (let j = i + 1; j < cfg.CELLS.length; j++) {
      const a = cfg.CELLS[i], b = cfg.CELLS[j];
      if (Math.hypot(a.x - b.x, a.z - b.z) > 150) continue;
      scene.add(new THREE.Line(
        new THREE.BufferGeometry().setFromPoints([
          new THREE.Vector3(a.x, GND_Y, a.z),
          new THREE.Vector3(b.x, GND_Y, b.z),
        ]),
        new THREE.LineBasicMaterial({ color: 0x004488, transparent: true, opacity: 0.38,
          blending: THREE.AdditiveBlending, depthWrite: false })
      ));
      const forward = (i + j) % 2 === 0;
      const dotMat = new THREE.MeshBasicMaterial({ color: 0xffee44, transparent: true, opacity: 0,
        blending: THREE.AdditiveBlending, depthWrite: false });
      const dot = new THREE.Mesh(new THREE.SphereGeometry(0.26, 6, 5), dotMat);
      scene.add(dot);
      GROUND_CONNS.push({
        dot, dotMat,
        ax: forward ? a.x : b.x, az: forward ? a.z : b.z,
        bx: forward ? b.x : a.x, bz: forward ? b.z : a.z,
        phase: (i * 0.618 + j * 0.382) % 1.0,
        speed: 0.042 + ((i * 5 + j) % 6) * 0.012,
      });
    }
  }

  // ── Animate ──
  const clock = new THREE.Clock();
  let lastMs = 0;

  function animate() {
    _animId = requestAnimationFrame(animate);
    const now = performance.now();
    if (now - lastMs < 30) return;
    lastMs = now;
    const t = clock.getElapsedTime();

    camera.position.x = target.x + Math.sin(theta) * Math.cos(phi) * radius;
    camera.position.y = target.y + Math.sin(phi) * radius;
    camera.position.z = target.z + Math.cos(theta) * Math.cos(phi) * radius;
    camera.lookAt(target);
    theta += 0.00012;

    const bon = (t % 1.6) < 0.12;
    TOWER_REFS.forEach(r => {
      if (!r) return;
      r.bMat.emissiveIntensity = bon ? 18 : 0.3;
      r.bLight.intensity       = bon ? 3.5 : 0;
    });

    HOLO_RINGS.forEach(hr => {
      if (hr.tilt === 'x') hr.mesh.rotation.x += hr.speed * 0.018;
      else                  hr.mesh.rotation.z += hr.speed * 0.018;
      hr.mesh.rotation.y += hr.speed * 0.012;
    });

    SCAN_RINGS.forEach(sr => {
      const ph = ((t * sr.speed + sr.phase) % 1.0);
      sr.mesh.position.y = sr.minY + (sr.maxY - sr.minY) * ph;
      sr.mat.opacity = Math.sin(ph * Math.PI) * 0.72;
    });

    ARC_PACKETS.forEach(p => {
      const ph = ((t * p.speed + p.phase) % 1.0);
      const pos = p.forward ? p.curve.getPoint(ph) : p.curve.getPoint(1 - ph);
      p.mesh.position.copy(pos);
      p.mat.opacity = Math.sin(ph * Math.PI) * 0.95;
      if (_net.cells[p.tiA]) p.mat.color.set(loadHex(_net.cells[p.tiA].load));
    });

    UE_GROUPS.forEach((u, i) => {
      let nx, nz;
      const real = _ueTargets && _ueTargets[i];
      if (real) {
        u.g.position.x += (real.x - u.g.position.x) * 0.04;
        u.g.position.z += (real.z - u.g.position.z) * 0.04;
        nx = u.g.position.x; nz = u.g.position.z;
        u.towerX = real.towerX; u.towerY = real.towerY; u.towerZ = real.towerZ;
        if (real.cellColor) {
          const tc = new THREE.Color(real.cellColor);
          u.coreMat.color.lerp(tc, 0.06);
          u.coreMat.emissive.lerp(tc, 0.06);
          u.wireMat.color.lerp(tc, 0.06);
          u.glowMat.color.lerp(tc, 0.06);
        }
        if (u.beamMat) {
          u.beamMat.color.setHex(real.cellColor || 0x00ccff);
          u.beamMat.opacity = 0.50 + Math.sin(t * 4 + i * 0.7) * 0.18;
        }
      } else {
        const dx = u.wanderX - u.g.position.x;
        const dz = u.wanderZ - u.g.position.z;
        const dist = Math.sqrt(dx * dx + dz * dz);
        if (dist < 5) {
          u.wanderX = (Math.random() - 0.5) * 180;
          u.wanderZ = (Math.random() - 0.5) * 180;
        }
        const spd = 0.22 + (i % 5) * 0.04;
        nx = u.g.position.x + (dx / (dist + 0.01)) * spd;
        nz = u.g.position.z + (dz / (dist + 0.01)) * spd;
        u.g.position.x = nx; u.g.position.z = nz;
        u.g.rotation.y = Math.atan2(dx, dz);
      }
      u.g.position.y = u.baseY + Math.sin(t * 1.1 + i * 0.9) * 0.28;
      u.g.rotation.x = Math.sin(t * 0.7 + i * 1.3) * 0.18;
      const pos = u.beamGeo.attributes.position.array;
      pos[0] = u.towerX; pos[1] = u.towerY; pos[2] = u.towerZ;
      pos[3] = nx; pos[4] = u.g.position.y; pos[5] = nz;
      u.beamGeo.attributes.position.needsUpdate = true;
    });

    const nowMs = performance.now();
    for (let fi = HANDOVER_FLASHES.length - 1; fi >= 0; fi--) {
      const f = HANDOVER_FLASHES[fi];
      const age = (nowMs - f.startMs) / 1000;
      if (age < 0) continue;
      if (f.isArc) {
        f.mat.opacity = Math.max(0, 0.88 - age * 2.8);
        if (age > 0.38) {
          _scene.remove(f.mesh); f.mesh.geometry.dispose(); f.mat.dispose();
          HANDOVER_FLASHES.splice(fi, 1);
        }
        continue;
      }
      f.mesh.scale.set(Math.min(age * 12, 10), Math.min(age * 12, 10), 1);
      f.mat.opacity = Math.max(0, 0.85 - age * 1.3);
      if (age > 0.8) {
        _scene.remove(f.mesh); f.mesh.geometry.dispose(); f.mat.dispose();
        HANDOVER_FLASHES.splice(fi, 1);
      }
    }

    radarGroup.rotation.y += 0.0055;

    GROUND_CONNS.forEach(c => {
      const ph = ((t * c.speed + c.phase) % 1.0);
      c.dot.position.set(c.ax + (c.bx - c.ax) * ph, GND_Y + 0.18, c.az + (c.bz - c.az) * ph);
      c.dotMat.opacity = Math.sin(ph * Math.PI) * 0.92;
    });

    // Coverage rings gently breathe
    COVERAGE_RINGS.forEach(cr => {
      cr.mat.opacity = cr.base * (0.55 + 0.45 * Math.sin(t * cr.speed + cr.phase));
    });

    // Atmospheric particles drift slowly
    for (let i = 0; i < ATMO_N; i++) {
      aAng[i] += aSpd[i] * 0.010;
      const dft = Math.sin(t * 0.22 + aPhase[i]) * 5;
      aPos[i*3]   = Math.cos(aAng[i]) * (aRad[i] + dft);
      aPos[i*3+1] = aInitY[i] + Math.sin(t * 0.16 + aPhase[i]) * 2.2;
      aPos[i*3+2] = Math.sin(aAng[i]) * (aRad[i] + dft);
    }
    atmoGeo.attributes.position.needsUpdate = true;

    _composer.render();
  }
  animate();

  function updateColors() {
    TOWER_REFS.forEach((refs, ti) => {
      if (!refs || !_net.cells[ti]) return;
      const c = new THREE.Color(loadHex(_net.cells[ti].load));
      refs.leds.forEach(m   => { m.color.copy(c); if (m.emissive) m.emissive.copy(c); });
      refs.lights.forEach(l => l.color.copy(c));
      refs.halos.forEach(m  => m.color.copy(c));
      refs.fans.forEach(m   => m.color.copy(c));
    });
  }

  return { updateColors };
}
