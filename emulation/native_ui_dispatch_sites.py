"""Original main UI loops only; never replace nested timer helper calls."""
import hashlib,struct
BASE=0x40160000
PROFILES={
 'zr-c110':dict(sha256='606d6116dc8e1560fb1608743a18840e0f7ab2617d11bcf02af025405aca1542',
  poll=0x403e429c,wait=0x403e4418,event_wait=0x403e8b48,event_group=0x403e8a5c,current_task=0x401865f0,
  event_set=0x403e8b28,event_set_native=0x40f40908,wake_reference=0x403e9dd0,wake_mask=0x200000,
  loops=[(0x4040f9e4,0x4040fa38),(0x40412e14,0x40412e5c),(0x40528234,0x40528274),
         (0x40649720,0x406497e0),(0x4064bc60,0x4064bcb8)]),
 'z9-c532':dict(sha256='6380ded18395ce11cb327c766721274c85127d2cf1a1e887ee60e3d156810bdd',
  poll=0x403d2158,wait=0x403d22d4,event_wait=0x403d69f4,event_group=0x403d6908,current_task=0x401865d8,
  event_set=0x403d69d4,event_set_native=0x40d2fcdc,wake_reference=0x403d7c50,wake_mask=0x200000,
  argument_setups={0x403fc7b4:0x403fc794,0x405009b4:0x40500998},
  loops=[(0x403f97ec,0x403f9844),(0x403fc768,0x403fc7b4),(0x40500970,0x405009b4),
         (0x406120e0,0x406121a4),(0x406143cc,0x40614428)])}
def call(address,target):
 delta=target-address
 if delta%4 or not -(1<<27)<=delta<(1<<27):raise ValueError('UI call out of range')
 return struct.pack('<I',0x94000000|((delta//4)&0x3ffffff))
def sites(target):
 return {address:'fx_native_ui_'+kind+'_native' for pair in PROFILES[target]['loops'] for address,kind in zip(pair,['wait','poll'])}
def verify_original(target,data):
 p=PROFILES[target]
 if hashlib.sha256(data).hexdigest()!=p['sha256']:raise ValueError('original UI profile identity differs')
 def block(a,n):return data[a-BASE:a-BASE+n]
 for wait,poll in p['loops']:
  if block(wait,4)!=call(wait,p['wait']) or block(poll,4)!=call(poll,p['poll']):raise ValueError('UI main loop call differs')
  if block(wait-8,4)!=call(wait-8,p['event_group']) or block(wait+16,4)!=call(wait+16,p['event_wait']):raise ValueError('UI event-group wait edge differs')
  if block(wait+4,4)!=bytes.fromhex('e203002a'):raise ValueError('native timeout not passed as w2')
  start=p.get('argument_setups',{}).get(poll,poll-4)
  if struct.unpack('<I',block(start,4))[0]&0xffc003ff!=0x910003e0:raise ValueError('native flags not an original stack array')
  # C532 inlines two memset sequences after ADD x0,SP. Permit only explicit
  # zero stores to SP with no writeback/call/register mutation in that gap.
  for at in range(start+4,poll,4):
   word=struct.unpack('<I',block(at,4))[0]
   if word&0xffc003ff not in (0x390003ff,0x790003ff,0xf90003ff) and word&0xffc07fff!=0xa9007fff:
    raise ValueError('original flags pointer clobbered before pump')
 if hashlib.sha256(block(p['current_task'],8)).hexdigest()!='42810809c1288bee7e5f0b8743a134e2486a4b93ea7041006c6c220d13d02298':raise ValueError('original TPIDR task ABI differs')
 a=p['wake_reference'];setter=p['event_set']
 if block(a,12)!=call(a,p['event_group'])+bytes.fromhex('e1030b32')+call(a+8,setter):
  raise ValueError('original UI coalescing wake producer differs')
 branch=struct.pack('<I',0x14000000|(((p['event_set_native']-(setter+8))//4)&0x3ffffff))
 if hashlib.sha256(block(setter,8)).hexdigest()!='22755fbc7b9ff15cbf25d362629d4fac4657bb7440248e22574b914627c4506d' or block(setter+8,4)!=branch or struct.unpack('<I',block(setter+12,4))[0]!=0xd65f03c0:
  raise ValueError('original event-group setter ABI differs')
 return dict(target=target,original_sha256=p['sha256'],main_loops=5,main_loop_calls=10,task_pointer_word=0,
  fixed_timer_slots_used=0,nested_timer_entry_replaced=False,wake_mask=p['wake_mask'],wake_reference=hex(a))
