/* PCM1 framing is shared with pcm_ring.c. Works on HTTP LAN pages without AudioWorklet. */
(function(root){
'use strict';
class PCMDecoder {
 constructor(onBlock){this.onBlock=onBlock;this.header=new Uint8Array(24);this.pcm=new Uint8Array(3840);this.h=0;this.n=0;this.info=null}
 push(chunk){
  let pos=0;
  while(pos<chunk.length){
   if(!this.info){
    const count=Math.min(24-this.h,chunk.length-pos);this.header.set(chunk.subarray(pos,pos+count),this.h);this.h+=count;pos+=count;
    if(this.h!==24)continue;
    const v=new DataView(this.header.buffer);
    if(v.getUint32(0,true)!==0x314d4350||v.getUint32(8,true)!==48000||v.getUint16(12,true)!==2||v.getUint16(14,true)!==16||v.getUint32(16,true)!==3840||!v.getUint32(20,true))throw Error('Unsupported audio packet');
    this.info={sequence:v.getUint32(4,true),generation:v.getUint32(20,true),rate:48000,channels:2};
   }else{
    const count=Math.min(3840-this.n,chunk.length-pos);this.pcm.set(chunk.subarray(pos,pos+count),this.n);this.n+=count;pos+=count;
    if(this.n===3840){this.onBlock(this.info,this.pcm.slice());this.info=null;this.h=0;this.n=0}
   }
  }
 }
 finish(){if(this.h||this.n||this.info)throw Error('Incomplete audio packet')}
}
root.PCMDecoder=PCMDecoder;
if(typeof module==='object'&&module.exports)module.exports={PCMDecoder};
if(!root.document)return;
const button=document.getElementById('sound'),note=document.getElementById('audio-state');
const browserVolume=document.getElementById('browser-volume'),speakerVolume=document.getElementById('speaker-volume'),speakerMute=document.getElementById('speaker-mute');
let enabled=false,context,gain,controller,retry,nextTime=0,previous=null,nodes=new Set(),runId=0;
function flush(){for(const node of nodes){try{node.stop()}catch(e){}}nodes.clear();nextTime=0;previous=null}
function play(info,pcm){
 if(!enabled||context.state!=='running')return;
 if(previous&&(info.generation!==previous.generation||info.sequence!==((previous.sequence+1)>>>0)))flush();
 const now=context.currentTime;
 if(nextTime>now+0.15)flush();
 if(nextTime<now+0.005)nextTime=now+0.04;
 const audio=context.createBuffer(2,960,48000),view=new DataView(pcm.buffer,pcm.byteOffset,pcm.byteLength);
 for(let channel=0;channel<2;channel++){
  const samples=audio.getChannelData(channel);
  for(let i=0;i<960;i++)samples[i]=view.getInt16((i*2+channel)*2,true)/32768;
 }
 const source=context.createBufferSource();source.buffer=audio;source.connect(gain);nodes.add(source);
 source.onended=()=>{nodes.delete(source);source.disconnect()};source.start(nextTime);nextTime+=0.02;previous=info;
 note.textContent='HDMI audio · 48 kHz stereo';
}
async function listen(id){
 if(!enabled||id!==runId)return;
 controller=new AbortController();const current=controller;
 const timer=setTimeout(()=>current.abort(),5000);
 try{
  const response=await fetch('http://'+location.hostname+':82/audio.pcm',{cache:'no-store',signal:current.signal});
  clearTimeout(timer);
  if(!response.ok)throw Error(response.status===503?'Waiting for HDMI audio':'Audio HTTP '+response.status);
  const reader=response.body.getReader(),decoder=new PCMDecoder(play);
  while(enabled&&id===runId){
   const idle=setTimeout(()=>current.abort(),1000);
   let part;try{part=await reader.read()}finally{clearTimeout(idle)}
   if(part.done){decoder.finish();break}decoder.push(part.value);
  }
  if(enabled&&id===runId)throw Error('Audio stream ended');
 }catch(error){if(enabled&&id===runId)note.textContent=error.name==='AbortError'?'Audio connection timed out':error.message}
 finally{
  clearTimeout(timer);current.abort();
  if(enabled&&id===runId){flush();retry=setTimeout(()=>listen(id),1000)}
 }
}
button.onclick=async()=>{
 if(enabled){enabled=false;++runId;clearTimeout(retry);controller?.abort();flush();await context.suspend();button.textContent='Enable sound';note.textContent='Browser audio off';return}
 const Audio=root.AudioContext||root.webkitAudioContext;
 if(!Audio){note.textContent='Web Audio is unavailable in this browser';return}
 try{
  if(!context){context=new Audio({latencyHint:'interactive'});gain=context.createGain();gain.connect(context.destination)}
  gain.gain.value=Number(browserVolume.value)/100;
  await context.resume();enabled=true;const id=++runId;button.textContent='Disable sound';note.textContent='Connecting to HDMI audio…';listen(id);
 }catch(error){note.textContent='Could not enable audio: '+error.message}
};
browserVolume.oninput=()=>{if(gain)gain.gain.value=Number(browserVolume.value)/100};
let speakerDirty=false;
speakerVolume.oninput=()=>{speakerDirty=true};
speakerMute.onchange=()=>{speakerDirty=true;setSpeaker()};
speakerVolume.onchange=setSpeaker;
async function setSpeaker(){
 try{
  const response=await fetch('/audio/control',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
   body:'volume='+speakerVolume.value+'&mute='+(speakerMute.checked?'1':'0')});
  if(!response.ok)throw Error('HTTP '+response.status);speakerDirty=false;
 }catch(error){note.textContent='Speaker control failed: '+error.message}
}
async function status(){
 try{
  const response=await fetch('/audio/status',{cache:'no-store'});if(!response.ok)throw Error('HTTP '+response.status);
  const value=await response.json();
  button.disabled=!value.enabled;speakerVolume.disabled=!value.speaker_ready;speakerMute.disabled=!value.speaker_ready;
  if(!speakerDirty){speakerVolume.value=value.volume;speakerMute.checked=value.mute}
  if(!enabled)note.textContent=value.enabled?(value.connected?'HDMI audio available · click Enable sound':'Waiting for MS2109 HDMI audio'):'Audio capture disabled or unavailable';
 }catch(error){if(!enabled)note.textContent='Audio status unavailable'}
 setTimeout(status,1000);
}
root.addEventListener('pagehide',()=>{enabled=false;++runId;clearTimeout(retry);controller?.abort();flush();context?.close()});
status();
})(typeof window==='object'?window:globalThis);
