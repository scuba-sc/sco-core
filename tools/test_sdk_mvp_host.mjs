// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {once} from 'node:events';
const [hostPath, serverPath, pluginRoot, dataRoot] = process.argv.slice(2);
const deadline = AbortSignal.timeout(30000);
const host = spawn(hostPath, ['--port','0','--bind','127.0.0.1','--plugins',pluginRoot,
  '--data',dataRoot,'--test-shutdown'], {stdio:['ignore','pipe','pipe']});
const hostExit = once(host,'exit',{signal:deadline});
const lines = createInterface({input:host.stdout});
let logs = '', server;
host.stderr.on('data', b => logs += b);
try {
  const port = await new Promise((resolve,reject) => {
    lines.on('line', line => { logs += line+'\n'; const m=/listening port=(\d+)/.exec(line); if(m)resolve(Number(m[1])); });
    host.once('exit', code => reject(new Error('host exited '+code+' '+logs)));
    deadline.addEventListener('abort',()=>reject(deadline.reason),{once:true});
  });
  const url='ws://127.0.0.1:'+port+'/ws';
  server=spawn('node',[serverPath,'--host',url],{stdio:['pipe','pipe','pipe']});
  const serverExit=once(server,'exit',{signal:deadline});
  const replies=createInterface({input:server.stdout});
  let sequence=0;const pending=new Map();
  replies.on('line',line=>{const v=JSON.parse(line);pending.get(v.id)?.(v);pending.delete(v.id);});
  function request(method,params={}) {
    const id=++sequence;
    return new Promise((resolve,reject)=>{
      pending.set(id,resolve);
      deadline.addEventListener('abort',()=>reject(deadline.reason),{once:true});
      server.stdin.write(JSON.stringify({jsonrpc:'2.0',id,method,params})+'\n');
    });
  }
  await request('initialize',{protocolVersion:'2025-11-25',capabilities:{},clientInfo:{name:'sdk-mvp',version:'1'}});
  server.stdin.write(JSON.stringify({jsonrpc:'2.0',method:'notifications/initialized'})+'\n');
  async function call(name,args={}) {
    const r=await request('tools/call',{name,arguments:args});assert(!r.error,JSON.stringify(r));
    assert.equal(r.result.isError,false,JSON.stringify(r));return JSON.parse(r.result.content[0].text);
  }
  const status=await call('host_status');
  assert(status.kernel.plugins.some(p=>p.id==='mvp_probe'&&p.state==='loaded'));
  const result=await call('invoke_command',{name:'mvp_probe.ping',args:[]});
  assert.equal(result.result,'OK');assert.equal(result.reply,'pong');
  const panel=await call('get_widget_panel',{panel_id:'mvp_probe.main'});
  assert(panel.nodes.some(n=>n.kind==='button'));
  const admin=new WebSocket(url);await once(admin,'open',{signal:deadline});
  admin.send(JSON.stringify({id:1,method:'system.shutdown',params:{}}));
  assert.equal((await hostExit)[0],0);admin.close();
  server.stdin.end();assert.equal((await serverExit)[0],0);replies.close();
  console.log('PASS: MCP-generated archive-only plugin loaded; agent MCP ping=pong; retained panel read; clean host/server shutdown');
} finally {
  lines.close();if(server&&server.exitCode===null)server.kill();if(host.exitCode===null)host.kill();
}
