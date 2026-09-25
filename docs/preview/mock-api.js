// Local demonstration data. This script is only injected into docs/preview/index.html.
(() => {
  const names = {
    11:'Hipower', 12:'USA Mk IV', 13:'USA IIC+', 14:'Recto 1', 15:'Recto 2',
    16:'Euro', 17:'AC-20 Deluxe', 18:'Shiver', 19:'Brit 800', 20:'Clean Chorus'
  };
  const sceneNames = ['Orange Vintage','Red Vintage','Orange Modern','Red Modern',
    'Driven Orange','Driven Red','Dry Orange','Red Lead'];
  const labels = ['CMP','DRV','MOD','DLY','REV','BST'];
  const customMidi = Array.from({length:2}, (_, set) => Array.from({length:6}, (_, slot) => ({
    mode:slot === 0 ? 'alternate' : 'single', name:slot === 0 ? (set ? 'Tap Tempo' : 'Boost') : '',
    nextBank:0,
    banks:[[{cmd:'CC',channel:1,number:set ? 14 : 20 + slot,value:127}],
      [{cmd:'CC',channel:1,number:set ? 14 : 20 + slot,value:0}]]
  })));
  const state = {
    scanning:false, scanProgress:0, presetCount:501, cacheRevision:1,
    unsaved:false, storageReady:true, ready:true, presetNumber:15,
    presetName:'Recto 2', sceneNumber:1, sceneName:sceneNames[0], message:'Bereit',
    favoriteModeEnabled:true, favoriteModeActive:true, favoritesRevision:1,
    favorites:[11,12,13,14,15,16], mode:'effects', customMidiRevision:1,
    customMidiDirty:false, activeCustomMidiSet:0, customMidi,
    scenes:sceneNames.map((name, index) => ({number:index+1,name,active:index===0})),
    slots:labels.map((label,index) => ({index,label,available:index!==5,active:index===4}))
  };
  const presets = Array.from({length:501}, (_, index) => ({
    name:names[index] || '', state:names[index] ? 1 : 0
  }));
  const network = {
    mode:'ap',activeMode:'ap',ssid:'',apSsid:'Lepus-DEMO01',address:'192.168.4.1',
    dhcp:true,hasPassword:true,ip:'',gateway:'',subnet:'',dns:''
  };
  window.fetch = async (input, options = {}) => {
    const url = new URL(input, location.href);
    const path = url.pathname;
    const params = url.searchParams;
    let result;
    if (path.endsWith('/api/status')) result = state;
    else if (path.endsWith('/api/presets')) result = {revision:state.cacheRevision,presets};
    else if (path.endsWith('/api/network')) result = network;
    else if (options.method === 'POST') {
      if (path.endsWith('/api/preset')) state.presetNumber = Number(params.get('number'));
      if (path.endsWith('/api/scene')) state.sceneNumber = Number(params.get('number'));
      if (path.endsWith('/api/effect')) {
        const slot = state.slots[Number(params.get('slot'))];
        if (slot?.available) slot.active = !slot.active;
      }
      if (path.endsWith('/api/command')) state.presetNumber = Math.max(0,Math.min(500,
        state.presetNumber + (params.get('cmd') === 'preset_up' ? 1 : -1)));
      if (path.endsWith('/api/favorite/mode')) state.favoriteModeEnabled = params.get('enabled') === '1';
      if (path.endsWith('/api/custom-midi/mode')) {
        const item = customMidi[Number(params.get('set'))][Number(params.get('slot'))];
        item.mode = params.get('mode') === '1' ? 'alternate' : 'single';
        state.customMidiRevision++;
      }
      if (path.endsWith('/api/custom-midi/name')) {
        customMidi[Number(params.get('set'))][Number(params.get('slot'))].name = params.get('name');
        state.customMidiRevision++;
      }
      if (path.endsWith('/api/custom-midi/save')) state.customMidiDirty = false;
      state.presetName = presets[state.presetNumber].name || 'Preset ' + state.presetNumber;
      state.sceneName = sceneNames[state.sceneNumber - 1] || '';
      state.scenes.forEach(scene => { scene.active = scene.number === state.sceneNumber; });
      result = {accepted:true};
    } else result = {error:'Unbekannter Demo-Endpunkt'};
    return {ok:!result.error,status:result.error?404:200,json:async()=>structuredClone(result)};
  };
})();
