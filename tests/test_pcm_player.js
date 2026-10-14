const fs=require('fs'),assert=require('assert');
const {PCMDecoder}=require('../firmware/main/pcm_player.js');
const fixture=new Uint8Array(fs.readFileSync('work/tests/audio-packet.bin'));
for(const size of [1,7,23,24,255,4096]){
 const frames=[],decoder=new PCMDecoder((info,data)=>frames.push({info,data}));
 for(let offset=0;offset<fixture.length;offset+=size)decoder.push(fixture.subarray(offset,offset+size));
 decoder.finish();assert.equal(frames.length,1);assert.equal(frames[0].info.sequence,7);assert.equal(frames[0].info.generation,3);
 const pcm=new DataView(frames[0].data.buffer);assert.equal(pcm.getInt16(0,true),32767);assert.equal(pcm.getInt16(2,true),-32768);
}
let frames=0;const many=new PCMDecoder(()=>frames++);many.push(new Uint8Array(Buffer.concat(Array(20).fill(Buffer.from(fixture)))));many.finish();assert.equal(frames,20);
for(const offset of [0,8,12,14,16,20]){
 const bad=fixture.slice();if(offset===20)bad.fill(0,20,24);else bad[offset]^=0xff;
 assert.throws(()=>new PCMDecoder(()=>{}).push(bad),/Unsupported/);
}
const partial=new PCMDecoder(()=>{});partial.push(fixture.subarray(0,100));assert.throws(()=>partial.finish(),/Incomplete/);
console.log('PASS: C-to-JavaScript PCM framing, arbitrary HTTP fragmentation, coalesced packets and malformed/truncated packets');
// Exercise the actual browser playback code: user gesture, stereo samples,
// contiguous scheduling under jitter, gain changes, and stop/flush on disable.
const vm=require('vm');
const elements=Object.fromEntries(['sound','audio-state','browser-volume','speaker-volume','speaker-mute'].map(id=>[id,{value:'50',checked:false,textContent:''}]));
let currentTime=0,reads=0;const sources=[],buffers=[];
class Context {
 constructor(){this.state='suspended'}
 get currentTime(){return currentTime}
 resume(){this.state='running';return Promise.resolve()}
 suspend(){this.state='suspended';return Promise.resolve()}
 close(){return Promise.resolve()}
 createGain(){return {gain:{value:1},connect(){}}}
 createBuffer(channels,frames,rate){assert.equal(channels,2);assert.equal(frames,960);assert.equal(rate,48000);const data=[new Float32Array(frames),new Float32Array(frames)];buffers.push(data);return {getChannelData:i=>data[i]}}
 createBufferSource(){const source={connect(){},disconnect(){},start(time){this.time=time},stop(){this.stopped=true}};sources.push(source);return source}
}
const second=fixture.slice();new DataView(second.buffer).setUint32(4,8,true);
const timers=new Map();let timerId=0;
const context={document:{getElementById:id=>elements[id]},AudioContext:Context,AbortController,location:{hostname:'device'},
 setTimeout:f=>{timers.set(++timerId,f);return timerId},clearTimeout:id=>timers.delete(id),addEventListener(){},
 fetch:async url=>url==='/audio/status'?{ok:true,json:async()=>({enabled:true,connected:true,speaker_ready:true,volume:35,mute:false})}:
 {ok:true,body:{getReader:()=>({read:()=>{reads++;if(reads===1)return Promise.resolve({value:fixture,done:false});if(reads===2){currentTime=0.023;return Promise.resolve({value:second,done:false})}return new Promise(()=>{})}})}}};
context.window=context;
(async()=>{
 vm.runInNewContext(fs.readFileSync('firmware/main/pcm_player.js','utf8'),context);
 await new Promise(setImmediate);assert.equal(reads,0); // No autoplay or audio fetch before a click.
 await elements.sound.onclick();await new Promise(setImmediate);
 assert.equal(sources.length,2);assert.equal(sources[0].time,0.04);assert.equal(sources[1].time,0.06);
 assert.equal(buffers[0][0][0],32767/32768);assert.equal(buffers[0][1][0],-1);
 assert.equal(elements.sound.textContent,'Disable sound');
 await elements.sound.onclick();assert(sources.every(source=>source.stopped));assert.equal(elements.sound.textContent,'Enable sound');
 console.log('PASS: browser audio gesture, signed stereo playback, contiguous jitter-buffer scheduling and mute/flush');
})().catch(error=>{console.error(error);process.exitCode=1});
