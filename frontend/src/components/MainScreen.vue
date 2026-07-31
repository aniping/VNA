<script setup lang="ts">
const toolbar = ['↶', '↷', 'Zoom', 'Max', '+ Trace', '+ Marker', 'Delete', 'Print', 'Save', 'Recall']
const softkeys = ['S-Parameters', 'Wave Quantities', 'Ratios', 'Receivers', 'More…']
const menus = ['File', 'Trace', 'Channel', 'Display', 'Tools', 'System', 'Help']
</script>

<template>
  <section class="main-screen" aria-label="Main measurement screen">
    <header class="toolbar">
      <button v-for="item in toolbar" :key="item" type="button">{{ item }}</button>
      <span class="toolbar-spacer" />
      <span class="instrument-title">Vector Network Analyzer</span>
    </header>

    <div class="measurement-workspace">
      <section class="diagram" aria-label="Cartesian measurement diagram">
        <header class="trace-strip">
          <span class="trace-index">Trc1</span>
          <span class="trace-name">No active trace</span>
        </header>
        <div class="plot-area">
          <span class="plot-reference">1</span>
          <span class="plot-empty">No measurement data</span>
        </div>
        <footer class="channel-row">
          <span>Ch1</span>
          <span>Start —</span>
          <span>Stop —</span>
          <span>Points —</span>
          <span>IFBW —</span>
          <span>Power —</span>
        </footer>
      </section>

      <aside class="softtool" aria-label="Softtool panel">
        <div class="softtool-tabs">
          <button class="active" type="button">Meas</button>
          <button type="button">Favorites</button>
        </div>
        <h2>Measurement</h2>
        <button v-for="item in softkeys" :key="item" type="button" class="softkey">
          <span>{{ item }}</span><span aria-hidden="true">›</span>
        </button>
        <div class="softtool-fill" />
        <button type="button" class="softkey close-key">Close</button>
      </aside>
    </div>

    <nav class="menu-bar" aria-label="Application menu">
      <button v-for="item in menus" :key="item" type="button">{{ item }}</button>
      <span class="menu-spacer" />
      <span class="status-pill offline">OFFLINE</span>
      <span>Revision —</span>
      <time>--:--</time>
    </nav>
  </section>
</template>
