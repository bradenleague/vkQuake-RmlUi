# RmlUI Data Binding Reference

## Text Interpolation
```html
<span>Score: {{ score }}</span>
<span>{{ player.name }}</span>
```

## Attribute Binding
```html
<img data-attr-sprite="item.icon"/>
<div data-attr-id="element_id"/>
```

## Conditional Attributes
```html
<input type="checkbox" data-attrif-disabled="is_locked"/>
```

## Class Binding
```html
<h1 data-class-highlight="score > 100">Score</h1>
```

## Style Binding
```html
<div data-style-opacity="health / 100"/>
<img data-style-image-color="team_color"/>
```

## Conditional Display
```html
<!-- Removes from DOM when false -->
<div data-if="has_item">You have the item!</div>

<!-- Hides but keeps in DOM -->
<div data-visible="show_hint">Hint text</div>
```

## Loops
```html
<div data-for="item : inventory">
  <span>{{ item.name }}</span>
</div>

<!-- With index -->
<div data-for="item, index : inventory">
  <span>{{ index + 1 }}. {{ item.name }}</span>
</div>
```

## Two-Way Binding (Forms)
```html
<input type="range" data-value="volume"/>
<input type="checkbox" data-checked="music_enabled"/>
<input type="text" data-value="player_name"/>
```

## Event Handlers
```html
<button data-event-click="start_game()">Start</button>
<button data-event-click="score = 0">Reset</button>
```

## RML Content Binding
```html
<div data-rml="is_enemy ? '<span class=\"danger\">Enemy!</span>' : 'Friend'"></div>
```

## Alias
```html
<div data-alias-title="t0" data-alias-icon="i0"></div>
```

## Expression Syntax
- **Operators are C-style**: use `&&`, `||`, `!` — NOT `and`, `or`, `not`
- In XML attributes, `&&` must be escaped as `&amp;&amp;` and `<` as `&lt;`
- Example: `data-if="health >= 60 &amp;&amp; health &lt; 80"`
- Comparison operators: `==`, `!=`, `<`, `>`, `<=`, `>=`
- Arithmetic: `+`, `-`, `*`, `/`
- Ternary: `condition ? value_a : value_b`

## Limitations
- Don't modify document structure within data models
- `DirtyVariable` applies to top-level model variables (dirty the parent binding for nested changes)
- Structural views (`data-for`, `data-if`) own generated subtrees; avoid manual DOM edits inside them
- `data-` bindings are initialized from markup; changing binding attributes at runtime may require rebuilding/reloading elements
- Event expressions depend on the active data model/event context and available callbacks
