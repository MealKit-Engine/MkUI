"use strict";
(() => {
  // src/convert.ts
  var kHeader = "# MealKit UI exported from Penpot";
  var kFormatLine = "@format ui 1";
  var weightNames = {
    "100": "Thin",
    "200": "ExtraLight",
    "300": "Light",
    "400": "Regular",
    "500": "Medium",
    "600": "SemiBold",
    "700": "Bold",
    "800": "ExtraBold",
    "900": "Black"
  };
  function num(v) {
    const r = Math.round(v * 100) / 100;
    return Object.is(r, -0) ? "0" : String(r);
  }
  function hex2(v) {
    return Math.max(0, Math.min(255, Math.round(v))).toString(16).padStart(2, "0").toUpperCase();
  }
  function color(c, opacity = 1) {
    if (!c) return "none";
    if (c.token) return "$" + c.token;
    const base2 = c.hex.replace("#", "").toUpperCase().padEnd(6, "0").slice(0, 6);
    const a = c.opacity * opacity;
    return "#" + base2 + (a >= 0.999 ? "" : hex2(a * 255));
  }
  function nodeName(name, fallback) {
    const s = name.trim().replace(/\s+/g, "_").replace(/[^A-Za-z0-9_.\-]/g, "");
    return s.length > 0 ? s : fallback;
  }
  function looksLikeKey(name) {
    return /^[A-Za-z][A-Za-z0-9_.\-]*$/.test(name) && name.includes(".");
  }
  function fontFile(run) {
    const family = run.fontFamily.replace(/\s+/g, "");
    let style = weightNames[run.fontWeight] ?? "Regular";
    if (run.fontStyle === "italic") style = style === "Regular" ? "Italic" : style + "Italic";
    return family + "-" + style;
  }
  var visibleFills = (n) => n.fills;
  function bakeReason(n) {
    if (n.data?.bake) return "marked to bake";
    if (n.type === "path" || n.type === "boolean" || n.type === "svg-raw" || n.type === "image") return n.type;
    if (n.mask) return "mask";
    if (n.type === "ellipse" && Math.abs(n.width - n.height) > 0.01) return "ellipse that is not a circle";
    if (n.blendMode && n.blendMode !== "normal") return "blend mode " + n.blendMode;
    const fills = visibleFills(n);
    if (fills.length > 1) return "more than one fill";
    if (fills.some((f) => f.image)) return "image fill";
    if (fills.some((f) => f.gradient && f.gradient.stops.length > 2)) return "gradient with more than two stops";
    if (n.strokes.length > 1) return "more than one stroke";
    if (n.strokes.some((s) => s.gradient || s.style !== "solid" && s.style !== "none")) return "dashed or gradient stroke";
    if (n.shadows.some((s) => s.style === "inner-shadow")) return "inner shadow";
    return "";
  }
  function visualLines(n, warn) {
    const out = /* @__PURE__ */ new Map();
    const fill = visibleFills(n)[0];
    if (n.type !== "text") {
      if (fill?.color) out.set("fill", ["fill " + color(fill.color)]);
      else if (fill?.gradient) {
        const g = fill.gradient;
        const a = g.stops[0], b = g.stops[g.stops.length - 1];
        const c0 = color({ hex: a.hex, opacity: a.opacity }), c1 = color({ hex: b.hex, opacity: b.opacity });
        if (g.type === "linear") out.set("fill", [`fill linear ${num(g.startX)} ${num(g.startY)} ${num(g.endX)} ${num(g.endY)} ${c0} ${c1}`]);
        else {
          const rx = Math.hypot(g.endX - g.startX, g.endY - g.startY);
          out.set("fill", [`fill radial ${num(g.startX)} ${num(g.startY)} ${num(rx)} ${num(rx * (g.width || 1))} ${c0} ${c1}`]);
        }
      } else out.set("fill", ["fill none"]);
      const stroke = n.strokes[0];
      if (stroke && stroke.color && stroke.width > 0) {
        if (stroke.alignment !== "inner") warn(`stroke alignment '${stroke.alignment}' is drawn inside`);
        out.set("stroke", [`stroke ${num(stroke.width)} ${color(stroke.color)}`]);
      } else out.set("stroke", ["stroke 0 none"]);
      const r = n.type === "ellipse" ? [n.width / 2, n.width / 2, n.width / 2, n.width / 2] : n.radius;
      if (n.radiusToken) out.set("radius", ["radius $" + n.radiusToken]);
      else if (r[0] === r[1] && r[1] === r[2] && r[2] === r[3]) out.set("radius", ["radius " + num(r[0])]);
      else out.set("radius", [`radius ${num(r[0])} ${num(r[1])} ${num(r[2])} ${num(r[3])}`]);
    }
    const shadows = n.shadows.filter((s) => s.style === "drop-shadow");
    out.set("shadow", shadows.length === 0 ? ["shadow none"] : ["shadow none", ...shadows.map((s) => `shadow ${num(s.offsetX)} ${num(s.offsetY)} ${num(s.blur)} ${num(s.spread)} ${color(s.color)}`)]);
    out.set("opacity", ["opacity " + num(n.opacity)]);
    if (n.type === "text" && n.text) {
      const base2 = baseRun(n.text.runs);
      if (base2?.fill) out.set("color", ["color " + color(base2.fill)]);
    }
    return out;
  }
  function baseRun(runs) {
    let best;
    for (const r of runs) if (!best || r.end - r.start > best.end - best.start) best = r;
    return best;
  }
  function runStyleKey(r) {
    return JSON.stringify([fontFile(r), r.fontSize, r.letterSpacing, r.textDecoration, r.fill ? color(r.fill) : ""]);
  }
  function textContent(runs, base2, fonts, errors, nodeLabel) {
    const tags = /* @__PURE__ */ new Map();
    const spans = [];
    let text2 = "";
    const baseKey = runStyleKey(base2);
    for (const r of runs) {
      const font2 = fontFile(r);
      if (!fonts.has(font2)) errors.push(`${nodeLabel}: font '${font2}' is not in assets/fonts`);
      const chars = r.characters.replace(/\r?\n/g, "\\n");
      const key = runStyleKey(r);
      if (key === baseKey || chars.trim().length === 0) {
        text2 += chars;
        continue;
      }
      let tag = tags.get(key);
      if (tag === void 0) {
        tag = tags.size + 1;
        tags.set(key, tag);
        const parts = [];
        if (font2 !== fontFile(base2)) parts.push("font " + font2);
        if (r.fontSize !== base2.fontSize) parts.push("size " + num(r.fontSize));
        if (r.fill && color(r.fill) !== (base2.fill ? color(base2.fill) : "")) parts.push("color " + color(r.fill));
        if (r.letterSpacing !== base2.letterSpacing) parts.push("letterSpacing " + num(r.letterSpacing));
        if (r.textDecoration === "underline" && base2.textDecoration !== "underline") parts.push("underline");
        if (r.textDecoration === "line-through" && base2.textDecoration !== "line-through") parts.push("strike");
        if (parts.length > 0) spans.push(`span ${tag} ${parts.join(" ")}`);
      }
      text2 += `<${tag}>${chars}</${tag}>`;
    }
    return { text: text2, spans };
  }
  function flowOrder(n) {
    return n.layout ? [...n.children].reverse() : n.children;
  }
  function indentOf(depth) {
    return "  ".repeat(depth);
  }
  function imagePath(ctx, name) {
    let base2 = `ui/${ctx.screen}/${name}`;
    let path = base2 + ".png";
    for (let i = 2; ctx.usedImages.has(path); ++i) path = `${base2}_${i}.png`;
    ctx.usedImages.add(path);
    return path;
  }
  function sizeWord(n, horizontal, parentLayout) {
    const v = horizontal ? n.width : n.height;
    if (n.type === "text" && n.text) {
      const s = parentLayout && n.child && !n.child.absolute ? horizontal ? n.child.horizontalSizing : n.child.verticalSizing : "fix";
      if (s === "fill") return "fill";
      if (n.text.growType === "auto-width") return "hug";
      if (n.text.growType === "auto-height" && !horizontal) return "hug";
      return num(v);
    }
    if (parentLayout && n.child && !n.child.absolute) {
      const s = horizontal ? n.child.horizontalSizing : n.child.verticalSizing;
      if (s === "fill") return "fill";
      if (s === "auto") return "hug";
      return num(v);
    }
    if (n.layout && (horizontal ? n.boardSizingH : n.boardSizingV) === "auto") return "hug";
    return num(v);
  }
  function emitPlacement(ctx, n, out, parentLayout, rotated) {
    const inFlow = parentLayout !== "none" && n.child !== void 0 && !n.child.absolute;
    if (parentLayout !== "none" && n.child?.absolute) out.push("absolute");
    if (!inFlow) out.push(`pos ${num(n.x)} ${num(n.y)}`);
    out.push(`size ${sizeWord(n, true, parentLayout !== "none")} ${sizeWord(n, false, parentLayout !== "none")}`);
    if (!inFlow && (n.constraintsH !== "left" || n.constraintsV !== "top")) out.push(`constraints ${n.constraintsH} ${n.constraintsV}`);
    if (inFlow && n.child) {
      const c = n.child;
      const m = c.margin;
      if (m.some((v) => v !== 0)) out.push(`margin ${m.map(num).join(" ")}`);
      if (c.alignSelf && c.alignSelf !== "auto") out.push("alignSelf " + c.alignSelf);
      if (parentLayout === "grid" && c.justifySelf && c.justifySelf !== "auto") out.push("justifySelf " + c.justifySelf);
      if (c.minWidth !== null || c.minHeight !== null) out.push(`min ${c.minWidth !== null ? num(c.minWidth) : "-"} ${c.minHeight !== null ? num(c.minHeight) : "-"}`);
      if (c.maxWidth !== null || c.maxHeight !== null) out.push(`max ${c.maxWidth !== null ? num(c.maxWidth) : "-"} ${c.maxHeight !== null ? num(c.maxHeight) : "-"}`);
    }
    if (parentLayout === "grid" && n.cell && inFlow) {
      const c = n.cell;
      out.push(c.rowSpan > 1 || c.columnSpan > 1 ? `cell ${c.row} ${c.column} ${c.rowSpan} ${c.columnSpan}` : `cell ${c.row} ${c.column}`);
    }
    if (n.rotation !== 0) {
      out.push("rotation " + num(n.rotation));
      if (!rotated) ctx.warnings.push(`${n.name}: rotated, the layout check skips it and its children`);
    }
    if (n.hidden) out.push("hidden");
  }
  function emitLayout(n, out) {
    const l = n.layout;
    if (!l) return;
    if (l.kind === "grid") {
      out.push(l.dir === "column" ? "layout grid column" : "layout grid");
      const track = (t) => t.type === "fixed" ? num(t.value) : t.type === "flex" ? num(t.value) + "fr" : t.type === "percent" ? num(t.value) + "%" : "auto";
      if (l.columns.length > 0) out.push("columns " + l.columns.map(track).join(" "));
      if (l.rows.length > 0) out.push("rows " + l.rows.map(track).join(" "));
      out.push("justifyItems " + l.justifyItems);
    } else {
      let word = l.dir.startsWith("column") ? "layout column" : "layout row";
      if (l.dir.endsWith("-reverse")) word += " reverse";
      if (l.wrap) word += " wrap";
      out.push(word);
    }
    if (l.rowGap !== 0 || l.columnGap !== 0) out.push(l.rowGap === l.columnGap ? "gap " + num(l.rowGap) : `gap ${num(l.rowGap)} ${num(l.columnGap)}`);
    if (l.padding.some((v) => v !== 0)) out.push("padding " + l.padding.map(num).join(" "));
    if (l.justifyContent !== "start") out.push("justify " + (l.kind === "flex" && l.justifyContent === "stretch" ? "start" : l.justifyContent));
    if (l.alignItems !== "start") out.push("align " + l.alignItems);
    if (l.alignContent !== "start") out.push("alignContent " + l.alignContent);
  }
  function emitData(d, out) {
    if (!d) return;
    if (d.visible) out.push("visible " + d.visible);
    if (d.disabled) out.push("disabled " + d.disabled);
    if (d.widthRatio) out.push("widthRatio " + d.widthRatio);
    if (d.colorField) out.push("colorField " + d.colorField);
    switch (d.widget) {
      case "button":
        if (d.action) out.push("button " + d.action);
        break;
      case "toggle":
        if (d.field) out.push(`toggle ${d.field}${d.action ? " " + d.action : ""}`);
        break;
      case "slider":
        if (d.field) out.push(`slider ${d.field} ${num(d.min ?? 0)} ${num(d.max ?? 1)} ${num(d.step ?? 0)}${d.action ? " " + d.action : ""}`);
        break;
      case "select":
        if (d.field && d.options) out.push(`select ${d.field} ${d.options.replace(/\s+/g, "")}${d.action ? " " + d.action : ""}`);
        break;
      case "textInput":
        if (d.field) out.push(`textInput ${d.field}${d.action ? " " + d.action : ""}`);
        if (d.placeholder) out.push("placeholder " + d.placeholder);
        if (d.maxLength) out.push("maxLength " + d.maxLength);
        break;
      case "scroll":
        out.push(d.scrollHorizontal ? "scroll horizontal" : "scroll");
        break;
      case "list":
        if (d.field) out.push("list " + d.field);
        break;
    }
    if (d.focusUp) out.push("focusUp " + d.focusUp);
    if (d.focusDown) out.push("focusDown " + d.focusDown);
    if (d.focusLeft) out.push("focusLeft " + d.focusLeft);
    if (d.focusRight) out.push("focusRight " + d.focusRight);
    if (d.transition !== void 0) out.push("transition " + num(d.transition));
  }
  function emitStates(ctx, n, base2, out) {
    for (const s of n.states ?? []) {
      const state = s.state.toLowerCase();
      if (!["hover", "pressed", "focus", "disabled", "checked"].includes(state)) {
        if (!["normal", "default"].includes(state)) ctx.warnings.push(`${n.name}: variant state '${s.state}' is not hover, pressed, focus, disabled or checked`);
        continue;
      }
      if (bakeReason(s.node)) ctx.warnings.push(`${n.name}: the ${state} variant cannot be drawn without baking (${bakeReason(s.node)}), its look is ignored`);
      const lines = visualLines(s.node, () => {
      });
      for (const [prop, v] of lines) {
        const b = base2.get(prop);
        if (b && b.join("\n") === v.join("\n")) continue;
        for (const line of v) out.push(`on ${state} ${line}`);
      }
      if (JSON.stringify(s.node.children.map((c) => visualLines(c, () => {
      }))) !== JSON.stringify(n.children.map((c) => visualLines(c, () => {
      }))))
        ctx.warnings.push(`${n.name}: the ${state} variant changes child layers, only the top layer's look is used`);
    }
  }
  function convertNode(ctx, n, depth, parentLayout, rotated, parentName) {
    const label = parentName ? `${parentName}/${n.name}` : n.name;
    const warn = (msg) => ctx.warnings.push(`${label}: ${msg}`);
    if (n.blur) warn("layer blur is not supported (ignored)");
    if (n.backgroundBlur) warn("background blur is not supported (ignored)");
    const name = nodeName(n.name, n.type + (ctx.count + 1));
    const ind = indentOf(depth);
    const props = [];
    const isRotated = rotated || n.rotation !== 0;
    if (!isRotated) ctx.boxes.push({ id: n.id, name, x: n.absX, y: n.absY, w: n.width, h: n.height });
    ctx.count++;
    if (n.type === "group" && !n.mask && parentLayout === "none" && !n.data && n.opacity === 1 && n.rotation === 0 && !n.hidden && n.shadows.length === 0 && !bakeReason(n)) {
      ctx.count--;
      ctx.boxes.pop();
      for (const c of n.children) convertNode(ctx, { ...c, x: c.x + n.x, y: c.y + n.y, constraintsH: c.constraintsH, constraintsV: c.constraintsV }, depth, "none", rotated, label);
      return;
    }
    const reason = bakeReason(n);
    const bakeWhole = reason !== "" && (n.children.length === 0 || n.type === "boolean" || n.type === "path" || n.type === "svg-raw" || n.mask || n.data?.bake === true);
    if (bakeWhole) {
      const path = imagePath(ctx, name);
      ctx.bakes.push({ id: n.id, path, scale: 2, skipChildren: false });
      ctx.lines.push(`${ind}image ${name}`);
      ctx.lines.push(`${ind}  id ${n.id}`);
      emitPlacement(ctx, n, props, parentLayout, rotated);
      props.push("src " + path);
      if (n.data?.slice) props.push("slice " + n.data.slice.map((v) => num(v * 2)).join(" "));
      if (n.opacity !== 1) props.push("opacity " + num(n.opacity));
      emitData(n.data, props);
      for (const p of props) ctx.lines.push(`${ind}  ${p}`);
      ctx.lines.push(`${ind}end`);
      return;
    }
    if (n.type === "text" && n.text) {
      const box = ctx.boxes[ctx.boxes.length - 1];
      if (box && box.id === n.id && n.text) box.text = n.text.characters.replace(/\r?\n/g, "\\n");
      ctx.lines.push(`${ind}text ${name}`);
      ctx.lines.push(`${ind}  id ${n.id}`);
      emitPlacement(ctx, n, props, parentLayout, rotated);
      const t = n.text;
      const base2 = baseRun(t.runs);
      if (!base2) {
        warn("empty text");
      } else {
        const { text: text2, spans } = textContent(t.runs, base2, ctx.fonts, ctx.errors, label);
        const key = n.data?.key || (looksLikeKey(n.name) ? n.name.trim() : "");
        if (key) {
          props.push("key " + key);
          ctx.strings.set(key, text2);
        } else {
          warn('the layer name is not a string key (like "menu.start"), so the text is not translated');
          props.push("content " + text2);
        }
        props.push("font " + fontFile(base2));
        props.push("fontSize " + num(base2.fontSize));
        if (base2.fill) props.push("color " + color(base2.fill));
        if (base2.gradientFill || t.runs.some((r) => r.gradientFill)) warn("gradient text is drawn with its first color");
        if (base2.letterSpacing !== 0) props.push("letterSpacing " + num(base2.letterSpacing));
        props.push("lineHeight " + num(base2.lineHeight));
        if (base2.textDecoration === "underline") props.push("underline");
        if (base2.textDecoration === "line-through") props.push("strike");
        if (base2.textTransform === "uppercase") props.push("transform upper");
        else if (base2.textTransform === "lowercase") props.push("transform lower");
        else if (base2.textTransform === "capitalize") warn("capitalize is not supported");
        if (t.align === "center" || t.align === "right") props.push("textAlign " + t.align);
        else if (t.align === "justify") warn("justified text is drawn left-aligned");
        if (t.verticalAlign === "center" || t.verticalAlign === "bottom") props.push("verticalAlign " + t.verticalAlign);
        props.push(...spans);
      }
      const d = n.data;
      if (d?.overflow) props.push("overflow " + d.overflow);
      if (d?.maxLines) props.push("maxLines " + d.maxLines);
      if (d?.wrap) props.push("wrap " + d.wrap);
      const visuals2 = visualLines(n, warn);
      for (const [prop, v] of visuals2) if (prop === "opacity" ? n.opacity !== 1 : prop === "shadow" && v.length > 1) props.push(...v.filter((l) => l !== "shadow none"));
      emitData(d, props);
      for (const p of props) ctx.lines.push(`${ind}  ${p}`);
      ctx.lines.push(`${ind}end`);
      return;
    }
    ctx.lines.push(`${ind}frame ${name}`);
    ctx.lines.push(`${ind}  id ${n.id}`);
    emitPlacement(ctx, n, props, parentLayout, rotated);
    if (n.clip && n.type === "board" && n.children.length > 0) props.push("clip");
    emitLayout(n, props);
    const visuals = visualLines(n, warn);
    if (reason) {
      const path = imagePath(ctx, name + "_bg");
      ctx.bakes.push({ id: n.id, path, scale: 2, skipChildren: true });
      for (const [prop, v] of visuals) if (prop === "opacity" && n.opacity !== 1) props.push(...v);
    } else {
      for (const [prop, v] of visuals) {
        if (prop === "fill" && v[0] === "fill none") continue;
        if (prop === "stroke" && v[0] === "stroke 0 none") continue;
        if (prop === "radius" && v[0] === "radius 0") continue;
        if (prop === "shadow" && v.length === 1) continue;
        if (prop === "opacity" && n.opacity === 1) continue;
        props.push(...v.filter((l) => !(prop === "shadow" && l === "shadow none")));
      }
      emitStates(ctx, n, visuals, props);
    }
    emitData(n.data, props);
    for (const p of props) ctx.lines.push(`${ind}  ${p}`);
    if (reason) {
      const bg = ctx.bakes[ctx.bakes.length - 1];
      const bgName = name + "_bg";
      ctx.lines.push(`${ind}  image ${bgName}`);
      ctx.lines.push(`${ind}    absolute`);
      ctx.lines.push(`${ind}    pos 0 0`);
      ctx.lines.push(`${ind}    size ${num(n.width)} ${num(n.height)}`);
      ctx.lines.push(`${ind}    constraints leftright topbottom`);
      ctx.lines.push(`${ind}    src ${bg.path}`);
      if (n.data?.slice) ctx.lines.push(`${ind}    slice ${n.data.slice.map((v) => num(v * 2)).join(" ")}`);
      ctx.lines.push(`${ind}  end`);
      ctx.count++;
      warn(`baked to ${bg.path} (${reason})`);
    }
    const layout = n.layout ? n.layout.kind : "none";
    for (const c of flowOrder(n)) convertNode(ctx, c, depth + 1, layout, isRotated, label);
    ctx.lines.push(`${ind}end`);
  }
  function convertBoard(board2, opt) {
    const screen = opt.file.replace(/^ui\//, "").replace(/\.ui$/, "");
    const ctx = {
      opt,
      fonts: new Set(opt.fonts),
      lines: [],
      bakes: [],
      strings: /* @__PURE__ */ new Map(),
      boxes: [],
      warnings: [],
      errors: [],
      count: 1,
      screen,
      usedImages: /* @__PURE__ */ new Set()
    };
    const name = nodeName(board2.name, screen);
    ctx.lines.push(`${kHeader}: ${opt.fileName} (file ${opt.fileId}, board ${board2.id}). Do not edit; change it in Penpot and export again.`);
    ctx.lines.push(kFormatLine);
    ctx.lines.push(`board ${name} ${num(board2.width)} ${num(board2.height)}`);
    ctx.lines.push(`  id ${board2.id}`);
    ctx.boxes.push({ id: board2.id, name, x: 0, y: 0, w: board2.width, h: board2.height });
    const d = board2.data;
    if (d?.navigation) ctx.lines.push("  navigation");
    if (d?.firstFocus) ctx.lines.push("  firstFocus " + d.firstFocus);
    if (d?.back) ctx.lines.push("  back " + d.back);
    const props = [];
    if (board2.clip && board2.children.length > 0) props.push("clip");
    emitLayout(board2, props);
    const reason = bakeReason(board2);
    if (reason) {
      ctx.warnings.push(`${board2.name}: the board background is baked (${reason})`);
      const path = imagePath(ctx, name + "_bg");
      ctx.bakes.push({ id: board2.id, path, scale: 1, skipChildren: true });
      props.push(`image ${name}_bg`, `  absolute`, `  pos 0 0`, `  size ${num(board2.width)} ${num(board2.height)}`, `  constraints scale scale`, `  src ${path}`, `end`);
      ctx.count++;
    } else {
      for (const [prop, v] of visualLines(board2, (m) => ctx.warnings.push(`${board2.name}: ${m}`))) {
        if (prop === "fill" && v[0] !== "fill none") props.push(...v);
        if (prop === "opacity" && board2.opacity !== 1) props.push(...v);
      }
    }
    emitData({ ...d, navigation: void 0, firstFocus: void 0, back: void 0 }, props);
    for (const p of props) ctx.lines.push("  " + p);
    const layout = board2.layout ? board2.layout.kind : "none";
    for (const c of flowOrder(board2)) convertNode(ctx, c, 1, layout, board2.rotation !== 0, board2.name);
    ctx.lines.push("end");
    if (ctx.count > opt.nodeBudget) ctx.warnings.push(`${ctx.count} nodes, over the budget of ${opt.nodeBudget} per screen`);
    return {
      ui: ctx.lines.join("\n") + "\n",
      bakes: ctx.bakes,
      strings: [...ctx.strings].map(([key, text2]) => ({ key, text: text2 })),
      boxes: ctx.boxes,
      warnings: ctx.warnings,
      errors: ctx.errors,
      nodeCount: ctx.count
    };
  }
  function convertTheme(tokens, fileName) {
    const lines = [`${kHeader}: design tokens of ${fileName}. Do not edit; change the tokens in Penpot and export again.`, kFormatLine];
    const warnings = [];
    const numberTypes = /* @__PURE__ */ new Set(["dimension", "spacing", "borderRadius", "sizing", "number", "opacity", "borderWidth", "fontSizes", "letterSpacing", "rotation"]);
    for (const t of [...tokens].sort((a, b) => a.name.localeCompare(b.name))) {
      const name = t.name.replace(/\s+/g, "-");
      if (t.type === "color") {
        const c = parseCssColor(t.value);
        if (c) lines.push(`color ${name} ${c}`);
        else warnings.push(`token ${t.name}: cannot read the color '${t.value}'`);
      } else if (numberTypes.has(t.type)) {
        const v = parseFloat(t.value);
        if (Number.isFinite(v)) lines.push(`number ${name} ${num(v)}`);
        else warnings.push(`token ${t.name}: cannot read the number '${t.value}'`);
      }
    }
    return { ui: lines.join("\n") + "\n", warnings };
  }
  function parseCssColor(v) {
    const s = v.trim();
    let m = /^#([0-9a-f]{3})$/i.exec(s);
    if (m) return "#" + [...m[1]].map((c) => c + c).join("").toUpperCase();
    m = /^#([0-9a-f]{6})([0-9a-f]{2})?$/i.exec(s);
    if (m) return ("#" + m[1] + (m[2] && m[2].toUpperCase() !== "FF" ? m[2] : "")).toUpperCase();
    m = /^rgba?\(\s*([\d.]+)\s*,\s*([\d.]+)\s*,\s*([\d.]+)\s*(?:,\s*([\d.]+%?)\s*)?\)$/i.exec(s);
    if (m) {
      let a = m[4] === void 0 ? 1 : m[4].endsWith("%") ? parseFloat(m[4]) / 100 : parseFloat(m[4]);
      a = Math.max(0, Math.min(1, a));
      return "#" + hex2(+m[1]) + hex2(+m[2]) + hex2(+m[3]) + (a >= 0.999 ? "" : hex2(a * 255));
    }
    return null;
  }

  // src/snapshot.ts
  var kDataKey = "mealkit";
  function readData(shape) {
    const raw = shape.getPluginData(kDataKey);
    if (!raw) return void 0;
    try {
      const d = JSON.parse(raw);
      return Object.keys(d).length > 0 ? d : void 0;
    } catch {
      return void 0;
    }
  }
  function fillColor(f, token) {
    if (!f || !f.fillColor) return void 0;
    return { hex: f.fillColor, opacity: f.fillOpacity ?? 1, token };
  }
  function snapFill(f, token) {
    if (f.fillImage) return { image: true };
    if (f.fillColorGradient) {
      const g = f.fillColorGradient;
      return {
        gradient: {
          type: g.type,
          startX: g.startX,
          startY: g.startY,
          endX: g.endX,
          endY: g.endY,
          width: g.width,
          stops: g.stops.map((s) => ({ hex: s.color, opacity: s.opacity ?? 1, offset: s.offset }))
        }
      };
    }
    return { color: fillColor(f, token) };
  }
  function numberOf(v, fallback) {
    const n = typeof v === "number" ? v : parseFloat(String(v ?? ""));
    return Number.isFinite(n) ? n : fallback;
  }
  var runProps = ["fontFamily", "fontWeight", "fontStyle", "fontSize", "lineHeight", "letterSpacing", "textTransform", "textDecoration"];
  function isMixed(r) {
    for (const p of runProps) if (r[p] === "mixed") return true;
    return r.fills === "mixed";
  }
  function runOf(r, start, end) {
    const fills = r.fills === "mixed" ? [] : r.fills;
    const fill = fills.find((f) => f.fillColor || f.fillColorGradient);
    return {
      start,
      end,
      characters: r.characters,
      fontFamily: String(r.fontFamily ?? ""),
      fontWeight: String(r.fontWeight ?? "400"),
      fontStyle: String(r.fontStyle ?? "normal"),
      fontSize: numberOf(r.fontSize, 14),
      lineHeight: numberOf(r.lineHeight, 1.2),
      letterSpacing: numberOf(r.letterSpacing, 0),
      textTransform: String(r.textTransform ?? "none"),
      textDecoration: String(r.textDecoration ?? "none"),
      fill: fill?.fillColor ? fillColor(fill) : fill?.fillColorGradient ? { hex: fill.fillColorGradient.stops[0]?.color ?? "#000000", opacity: 1 } : void 0,
      gradientFill: !!fill?.fillColorGradient
    };
  }
  function textRuns(t) {
    const out = [];
    const walk = (start, end) => {
      if (end <= start) return;
      const r = t.getRange(start, end);
      if (end - start === 1 || !isMixed(r)) {
        const run = runOf(r, start, end);
        const prev = out[out.length - 1];
        if (prev && JSON.stringify({ ...prev, start: 0, end: 0, characters: "" }) === JSON.stringify({ ...run, start: 0, end: 0, characters: "" })) {
          prev.end = end;
          prev.characters += run.characters;
        } else out.push(run);
        return;
      }
      const mid = start + end >> 1;
      walk(start, mid);
      walk(mid, end);
    };
    walk(0, t.characters.length);
    return out;
  }
  function shadowOf(s) {
    return {
      style: s.style ?? "drop-shadow",
      offsetX: s.offsetX ?? 0,
      offsetY: s.offsetY ?? 0,
      blur: s.blur ?? 0,
      spread: s.spread ?? 0,
      color: { hex: s.color?.color ?? "#000000", opacity: s.color?.opacity ?? 1 }
    };
  }
  function rangeOr(t, key, fallback) {
    if (t.characters.length === 0) return fallback;
    const v = t.getRange(0, t.characters.length)[key];
    return v && v !== "mixed" ? v : fallback;
  }
  function snapshot(shape, board2, opt = { variants: true }) {
    const tokens = shape.tokens ?? {};
    const fills = shape.fills === "mixed" ? [] : shape.fills;
    const n = {
      id: shape.id,
      name: shape.name,
      type: shape.type,
      x: shape === board2 ? 0 : shape.parentX,
      y: shape === board2 ? 0 : shape.parentY,
      absX: shape.x - board2.x,
      absY: shape.y - board2.y,
      width: shape.width,
      height: shape.height,
      rotation: shape.rotation ?? 0,
      opacity: shape.opacity ?? 1,
      blendMode: shape.blendMode ?? "normal",
      hidden: shape.hidden,
      clip: shape.type === "board" ? shape.clipContent : false,
      mask: shape.type === "group" ? shape.isMask() : false,
      constraintsH: shape.constraintsHorizontal ?? "left",
      constraintsV: shape.constraintsVertical ?? "top",
      fills: shape.type === "text" ? [] : fills.map((f) => snapFill(f, tokens["fill"])),
      strokes: shape.strokes.map((s) => ({
        color: s.strokeColor ? { hex: s.strokeColor, opacity: s.strokeOpacity ?? 1, token: tokens["strokeColor"] } : void 0,
        gradient: !!s.strokeColorGradient,
        width: s.strokeWidth ?? 1,
        alignment: s.strokeAlignment ?? "center",
        style: s.strokeStyle ?? "solid"
      })),
      shadows: shape.shadows.filter((s) => !s.hidden).map(shadowOf),
      blur: !!shape.blur && !shape.blur.hidden && (shape.blur.value ?? 0) > 0,
      backgroundBlur: !!shape.backgroundBlur && !shape.backgroundBlur.hidden && (shape.backgroundBlur.value ?? 0) > 0,
      radius: [shape.borderRadiusTopLeft ?? 0, shape.borderRadiusTopRight ?? 0, shape.borderRadiusBottomRight ?? 0, shape.borderRadiusBottomLeft ?? 0],
      // $token only when all four corners have the same token (numbers otherwise)
      radiusToken: tokens["borderRadiusTopLeft"] && ["borderRadiusTopRight", "borderRadiusBottomRight", "borderRadiusBottomLeft"].every((k) => tokens[k] === tokens["borderRadiusTopLeft"]) ? tokens["borderRadiusTopLeft"] : void 0,
      data: readData(shape),
      children: []
    };
    if (shape.type === "board") {
      const b = shape;
      n.boardSizingH = b.horizontalSizing;
      n.boardSizingV = b.verticalSizing;
      const l = b.grid ?? b.flex;
      if (l) {
        const grid = b.grid !== void 0 && b.grid !== null;
        n.layout = {
          kind: grid ? "grid" : "flex",
          dir: l.dir,
          wrap: !grid && b.flex?.wrap === "wrap",
          rowGap: l.rowGap ?? 0,
          columnGap: l.columnGap ?? 0,
          padding: [l.topPadding ?? 0, l.rightPadding ?? 0, l.bottomPadding ?? 0, l.leftPadding ?? 0],
          justifyContent: l.justifyContent ?? "start",
          alignItems: l.alignItems ?? "start",
          alignContent: l.alignContent ?? "start",
          justifyItems: l.justifyItems ?? "start",
          rows: grid ? b.grid.rows.map((t) => ({ type: t.type, value: t.value ?? 0 })) : [],
          columns: grid ? b.grid.columns.map((t) => ({ type: t.type, value: t.value ?? 0 })) : []
        };
      }
    }
    if (shape.layoutChild) {
      const c = shape.layoutChild;
      n.child = {
        absolute: c.absolute,
        horizontalSizing: c.horizontalSizing,
        verticalSizing: c.verticalSizing,
        alignSelf: c.alignSelf ?? "auto",
        justifySelf: "auto",
        margin: [c.topMargin ?? 0, c.rightMargin ?? 0, c.bottomMargin ?? 0, c.leftMargin ?? 0],
        minWidth: c.minWidth ?? null,
        maxWidth: c.maxWidth ?? null,
        minHeight: c.minHeight ?? null,
        maxHeight: c.maxHeight ?? null
      };
    }
    if (shape.layoutCell && shape.layoutCell.row !== void 0) {
      const c = shape.layoutCell;
      n.cell = { row: c.row ?? 1, column: c.column ?? 1, rowSpan: c.rowSpan ?? 1, columnSpan: c.columnSpan ?? 1 };
    }
    if (shape.type === "text") {
      const t = shape;
      n.text = {
        characters: t.characters,
        growType: t.growType,
        // Alignment lives on the paragraph (text range): the shape's align may differ from the paragraph
        align: String(rangeOr(t, "align", t.align) ?? "left"),
        verticalAlign: String(t.verticalAlign ?? "top"),
        runs: textRuns(t)
      };
    }
    if ("children" in shape && Array.isArray(shape.children)) {
      const kids = [...shape.children].sort((a, b) => a.parentIndex - b.parentIndex);
      n.children = kids.map((c) => snapshot(c, board2, { variants: opt.variants }));
    }
    if (opt.variants && shape.isComponentInstance()) {
      const comp = shape.component();
      if (comp && comp.isVariant() && comp.variants) {
        const prop = comp.variants.properties.find((p) => p.toLowerCase() === "state");
        if (prop) {
          const others = comp.variants.variantComponents();
          n.states = [];
          for (const v of others) {
            if (!v.isVariant()) continue;
            const value = v.variantProps[prop];
            if (!value || value === comp.variantProps[prop]) continue;
            const main = v.mainInstance();
            n.states.push({ state: value, node: snapshot(main, main, { variants: false }) });
          }
        }
      }
    }
    return n;
  }

  // src/encode.ts
  function pngSize(data) {
    if (data.length < 24 || data[0] !== 137 || data[1] !== 80) return null;
    const v = new DataView(data.buffer, data.byteOffset, data.byteLength);
    return { w: v.getUint32(16), h: v.getUint32(20) };
  }

  // src/example.ts
  var font = "Pretendard";
  function text(characters, name, size, color2) {
    const t = penpot.createText(characters);
    if (!t) throw new Error("createText failed");
    t.name = name;
    const f = penpot.fonts.findByName(font);
    if (f) f.applyToText(t, f.variants.find((v) => v.fontWeight === "500") ?? void 0);
    t.fontSize = String(size);
    t.fills = [{ fillColor: color2, fillOpacity: 1 }];
    t.growType = "auto-width";
    return t;
  }
  function board(name, w, h, color2, opacity = 1) {
    const b = penpot.createBoard();
    b.name = name;
    b.resize(w, h);
    b.fills = color2 ? [{ fillColor: color2, fillOpacity: opacity }] : [];
    return b;
  }
  function fillWidth(s) {
    if (s.layoutChild) s.layoutChild.horizontalSizing = "fill";
  }
  function createExample() {
    const root = board("pause", 1920, 1080, "#202634");
    root.x = 0;
    root.y = 0;
    root.setPluginData(kDataKey, JSON.stringify({ navigation: true, firstFocus: "resume", back: "pause.resume" }));
    const dim = penpot.createRectangle();
    dim.name = "dim";
    dim.resize(1920, 1080);
    dim.fills = [{ fillColor: "#05070c", fillOpacity: 0.6 }];
    root.appendChild(dim);
    dim.x = root.x;
    dim.y = root.y;
    dim.constraintsHorizontal = "leftright";
    dim.constraintsVertical = "topbottom";
    const panel = board("panel", 500, 400, "#10141c", 0.8);
    root.appendChild(panel);
    panel.x = root.x + 710;
    panel.y = root.y + 300;
    panel.borderRadius = 16;
    panel.strokes = [{ strokeColor: "#ffffff", strokeOpacity: 0.15, strokeWidth: 1, strokeAlignment: "inner", strokeStyle: "solid" }];
    panel.shadows = [{ style: "drop-shadow", offsetX: 0, offsetY: 12, blur: 40, spread: 0, color: { color: "#000000", opacity: 0.67 } }];
    panel.constraintsHorizontal = "center";
    panel.constraintsVertical = "center";
    const flex = panel.addFlexLayout();
    flex.dir = "column";
    flex.rowGap = 14;
    flex.topPadding = flex.bottomPadding = 32;
    flex.leftPadding = flex.rightPadding = 36;
    panel.verticalSizing = "auto";
    const title = text("Paused", "pause.title", 40, "#f2f4f8");
    panel.appendChild(title);
    fillWidth(title);
    title.getRange(0, title.characters.length).align = "center";
    for (const [name, label, action, color2] of [
      ["resume", "Resume", "pause.resume", "#ffc83d"],
      ["leave", "Leave", "pause.leave", "#2a3242"]
    ]) {
      const button = board(name, 428, 60, color2);
      button.borderRadius = 12;
      const row = button.addFlexLayout();
      row.dir = "row";
      row.justifyContent = "center";
      row.alignItems = "center";
      button.setPluginData(kDataKey, JSON.stringify({ widget: "button", action }));
      panel.appendChild(button);
      fillWidth(button);
      const t = text(label, "pause." + name, 28, name === "resume" ? "#10141c" : "#f2f4f8");
      button.appendChild(t);
    }
    const stats = board("stats", 428, 100, null);
    panel.appendChild(stats);
    fillWidth(stats);
    const grid = stats.addGridLayout();
    grid.addColumn("flex", 1);
    grid.addColumn("fixed", 120);
    grid.addRow("auto");
    grid.addRow("auto");
    grid.rowGap = 8;
    grid.columnGap = 16;
    stats.verticalSizing = "auto";
    const cells = [
      ["a", 1, 1],
      ["b", 1, 2],
      ["c", 2, 1]
    ];
    for (const [name, row, col] of cells) {
      const r = penpot.createRectangle();
      r.name = name;
      r.resize(60, 24);
      r.fills = [{ fillColor: "#ffffff", fillOpacity: 0.15 }];
      r.borderRadius = 4;
      grid.appendChild(r, row, col);
      if (name !== "b") fillWidth(r);
    }
    const wide = penpot.createRectangle();
    wide.name = "wide";
    wide.resize(60, 16);
    wide.fills = [{ fillColor: "#ffc83d", fillOpacity: 1 }];
    grid.appendChild(wide, 2, 2);
    const icon = penpot.createShapeFromSvg(
      '<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><path d="M6 4h4v16H6zM14 4h4v16h-4z" fill="#ffffff"/></svg>'
    );
    if (icon) {
      icon.name = "pause_icon";
      root.appendChild(icon);
      icon.x = root.x + 940;
      icon.y = root.y + 240;
    }
    panel.bringToFront();
    try {
      const catalog = penpot.library.local.tokens;
      const set = catalog.sets.find((s) => s.name === "mealkit") ?? catalog.addSet({ name: "mealkit", active: true });
      if (!set.active) set.toggleActive();
      const accent = set.tokens.find((t) => t.name === "accent") ?? set.addToken({ type: "color", name: "accent", value: "#ffc83d" });
      const resume = panel.children.find((c) => c.name === "resume");
      if (resume) resume.applyToken(accent, ["fill"]);
    } catch {
    }
    return root;
  }

  // src/plugin.ts
  var kFileKey = "mealkit.file";
  var kPreviewScale = 0.5;
  var base = "__MEALKIT_BASE__";
  var uiUrl = base.startsWith("__") ? "ui.html" : base + "ui.html";
  penpot.ui.open("MealKit", `${uiUrl}?theme=${penpot.theme}`, { width: 380, height: 680 });
  function send(msg) {
    penpot.ui.sendMessage(msg);
  }
  function isTopBoard(s) {
    return s.type === "board" && (!s.parent || s.parent.type !== "board" || s.parent.id === "00000000-0000-0000-0000-000000000000");
  }
  function topBoards() {
    const root = penpot.root;
    if (!root || !("children" in root)) return [];
    return root.children.filter((s) => s.type === "board");
  }
  function fileOf(board2) {
    const saved = board2.getPluginData(kFileKey);
    if (saved) return saved;
    return "ui/" + nodeName(board2.name, "screen").toLowerCase() + ".ui";
  }
  function sendSelection() {
    const layers = penpot.selection.map((s) => ({
      id: s.id,
      name: s.name,
      type: s.type,
      topBoard: isTopBoard(s),
      data: readData(s) ?? {},
      file: isTopBoard(s) ? fileOf(s) : ""
    }));
    send({ type: "selection", layers });
  }
  function sendBoards() {
    send({ type: "boards", boards: topBoards().map((b) => ({ id: b.id, name: b.name, file: fileOf(b) })) });
  }
  function findShape(id) {
    return penpot.currentPage?.getShapeById(id) ?? null;
  }
  function activeTokens() {
    const out = [];
    const catalog = penpot.library.local.tokens;
    if (!catalog) return out;
    for (const set of catalog.sets) {
      if (!set.active) continue;
      for (const t of set.tokens) {
        const type = t.type ?? "";
        const value = t.resolvedValueString;
        if (value !== void 0) out.push({ name: t.name, type, value });
      }
    }
    return out;
  }
  async function exportBoards(msg) {
    const file = penpot.currentFile;
    const fileName = file?.name ?? "Penpot";
    const screens = [];
    for (const id of msg.boardIds) {
      const board2 = findShape(id);
      if (!board2 || board2.type !== "board") continue;
      send({ type: "progress", text: `Reading ${board2.name}...` });
      const snap = snapshot(board2, board2);
      const path = fileOf(board2);
      const r = convertBoard(snap, { file: path, fileName, fileId: file?.id ?? "", fonts: msg.fonts, nodeBudget: msg.nodeBudget });
      const images = [];
      for (const b of r.bakes) {
        const shape = findShape(b.id);
        if (!shape) continue;
        send({ type: "progress", text: `Baking ${b.path}...` });
        try {
          const data = await shape.export({ type: "png", scale: b.scale, skipChildren: b.skipChildren });
          const size = pngSize(data);
          const logicalW = size ? size.w / b.scale : shape.width, logicalH = size ? size.h / b.scale : shape.height;
          if (Math.abs(logicalW - shape.width) > 1 || Math.abs(logicalH - shape.height) > 1)
            r.warnings.push(`${shape.name}: the baked image (${logicalW}x${logicalH}) is larger than the layer (${shape.width}x${shape.height}), effects outside the layer are squeezed in`);
          images.push({ path: b.path, data, logicalW, logicalH });
        } catch (e) {
          r.errors.push(`${shape.name}: could not export the PNG (${String(e)})`);
        }
      }
      let preview;
      try {
        preview = await board2.export({ type: "png", scale: kPreviewScale });
      } catch (e) {
        r.warnings.push(`could not export the preview image (${String(e)})`);
      }
      screens.push({ board: board2.name, file: path, ui: r.ui, images, strings: r.strings, boxes: r.boxes, preview, warnings: r.warnings, errors: r.errors, nodeCount: r.nodeCount });
    }
    let theme;
    if (msg.theme) {
      const tokens = activeTokens();
      if (tokens.length > 0) theme = convertTheme(tokens, fileName);
    }
    let source;
    let saveWarning = "";
    if (msg.source && file) {
      send({ type: "progress", text: "Saving the Penpot file..." });
      try {
        source = { name: nodeName(fileName, "ui") + ".penpot", data: await file.export("penpot", "all") };
      } catch (e) {
        send({ type: "progress", text: "" });
        saveWarning = `Could not save the Penpot file from the plugin (${String(e)}). Save it by hand: main menu > File > Export as .penpot, into art/ui/ of the project.`;
      }
    }
    if (saveWarning && screens.length > 0) screens[0].warnings.unshift(saveWarning);
    send({ type: "exported", screens, theme, source, fileName });
  }
  penpot.ui.onMessage((msg) => {
    switch (msg.type) {
      case "ready":
        send({ type: "init", fileName: penpot.currentFile?.name ?? "", fileId: penpot.currentFile?.id ?? "", theme: penpot.theme });
        sendSelection();
        sendBoards();
        break;
      case "setData": {
        const s = findShape(msg.id);
        if (!s) break;
        const clean = {};
        for (const [k, v] of Object.entries(msg.data)) if (v !== "" && v !== void 0 && v !== null && v !== false) clean[k] = v;
        s.setPluginData(kDataKey, Object.keys(clean).length > 0 ? JSON.stringify(clean) : "");
        break;
      }
      case "setFile": {
        const s = findShape(msg.id);
        if (s) s.setPluginData(kFileKey, msg.file);
        sendBoards();
        break;
      }
      case "select": {
        const s = findShape(msg.id);
        if (s) penpot.selection = [s];
        break;
      }
      case "export":
        exportBoards(msg).catch((e) => send({ type: "error", message: String(e) }));
        break;
      case "example":
        try {
          const b = createExample();
          penpot.selection = [b];
          sendBoards();
        } catch (e) {
          send({ type: "error", message: `Could not create the example: ${String(e)}` });
        }
        break;
    }
  });
  penpot.on("selectionchange", () => sendSelection());
  penpot.on("pagechange", () => sendBoards());
  penpot.on("themechange", (theme) => send({ type: "init", fileName: penpot.currentFile?.name ?? "", fileId: penpot.currentFile?.id ?? "", theme }));
})();
