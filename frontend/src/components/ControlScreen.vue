<script setup lang="ts">
const traceKeys = ['Meas', 'Format', 'Scale', 'Trace Config', 'Line', 'Marker']
const stimulusKeys = ['Start', 'Stop', 'Center', 'Span']
const channelKeys = ['Power / BW / Avg', 'Sweep', 'Cal', 'Channel Config', 'Trigger']
const systemKeys = ['Mode', 'Offset / Embed', 'File / Print', 'Setup', 'Tools', 'Display', 'Help', 'Preset']
const numberKeys = ['7', '8', '9', '4', '5', '6', '1', '2', '3', '±', '0', '.']
</script>

<template>
  <section class="control-screen" aria-label="Auxiliary control screen">
    <div class="hardkey-area">
      <div class="hardkey-column">
        <h2>TRACE</h2>
        <button v-for="item in traceKeys" :key="item" type="button">{{ item }}</button>
      </div>
      <div class="hardkey-column">
        <h2>STIMULUS</h2>
        <button v-for="item in stimulusKeys" :key="item" type="button">{{ item }}</button>
        <h2>CHANNEL</h2>
        <button v-for="item in channelKeys" :key="item" type="button">{{ item }}</button>
      </div>
      <div class="hardkey-column">
        <h2>SYSTEM</h2>
        <button v-for="item in systemKeys" :key="item" type="button" :class="{ help: item === 'Help' }">
          {{ item }}
        </button>
      </div>
    </div>

    <div class="entry-panel">
      <header>
        <span>Data Entry</span>
        <strong>—</strong>
      </header>
      <div class="entry-body">
        <div class="number-pad">
          <button v-for="item in numberKeys" :key="item" type="button">{{ item }}</button>
        </div>
        <div class="entry-actions">
          <button type="button">GHz / G</button>
          <button type="button">MHz / M</button>
          <button type="button">kHz / k</button>
          <button type="button">Hz / x1</button>
          <button type="button">⌫</button>
          <button type="button" class="cancel">Cancel</button>
        </div>
        <div class="navigation-pad">
          <button type="button">▲</button>
          <button type="button">◀</button>
          <button type="button" class="enter">Enter</button>
          <button type="button">▶</button>
          <button type="button">▼</button>
        </div>
      </div>
    </div>
  </section>
</template>
