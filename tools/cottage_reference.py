"""Independent SciPy HiGHS binary MILP for Mitsubishi HEAT16/OFF.
Run after building cottage_cli:
  python tools/cottage_reference.py --exe ./build/cottage_cli --short
All results synthetic; the MILP assumes indoor <15.5 C and is NOT a pump spec.
"""
import argparse
import json
import math
import subprocess
import time
from pathlib import Path
import numpy as np
from scipy.optimize import milp, Bounds, LinearConstraint
from scipy.sparse import coo_matrix

def scenario(name, n):
    d=dict(case=name,periods=n,status=0,initial_c=10.,initial_on=False,
        initial_minutes=180,mass=2.,loss=.12,heat_at_zero=3.2,
        heat_slope=.03,min_heat_kw=1.,cop_at_zero=2.8,cop_slope=.04,
        min_cop=1.2,max_cop=5.,heat_standby_kw=.012,
        off_standby_kw=.002,startup_kwh=.05,startup_delay_min=3.,
        switch_penalty=.03,min_heat_min=60,min_off_min=60,
        minimum_c=8.,maximum_c=14.5)
    d['slots']=[]
    for i in range(n):
        outdoor=-4+2*math.sin(2*math.pi*i/96)
        if name=='cold':outdoor-=6
        price=.07 if i%96<20 or i%96>=80 else .41
        if name=='price_spike' and 24<=i%96<72:price=.95
        derate=.65 if name=='defrost' and 12<=i%32<17 else 1.
        d['slots'].append([outdoor,price,0.,derate])
    return d

def inputs(executable, name, periods):
    if not executable:return scenario(name,periods)
    response=subprocess.run([str(executable),name,str(periods)],check=True,
        capture_output=True,text=True,timeout=30)
    data=json.loads(response.stdout)
    if data['status']!=0:raise RuntimeError("C++ MPC did not solve scenario")
    return data

def replay(d,actions):
    temperature=d['initial_c']; on=d['initial_on']
    age=d['initial_minutes']; minimum=temperature
    cost=0.;kwh=0.;thermal_kwh=0.;maximum=temperature;starts=0;violations=[]
    for i,(slot,command) in enumerate(zip(d['slots'],actions)):
        command=bool(command)
        if command!=on:
            if age<(d['min_heat_min'] if on else d['min_off_min']):
                violations.append('minimum_dwell_'+str(i))
            age=15
            if command:starts+=1
        else:age=min(600,age+15)
        outdoor,price,gain,derate=slot[:4]
        heat=max(d['min_heat_kw'],d['heat_at_zero']+d['heat_slope']*outdoor)*derate
        cop=max(d['min_cop'],min(d['max_cop'],d['cop_at_zero']+d['cop_slope']*outdoor))
        fraction=1.-d['startup_delay_min']/15 if command and not on else 1.
        if temperature>=16:fraction=0.
        elif temperature>15.5:fraction*=(16.-temperature)/.5
        thermal=heat*fraction if command else 0.
        alpha=math.exp(-d['loss']*.25/d['mass'])
        temperature=outdoor+(temperature-outdoor)*alpha+(thermal+gain)/d['loss']*(1-alpha)
        energy=.25*(d['heat_standby_kw'] if command else d['off_standby_kw'])
        if command:energy+=.25*thermal/cop
        if command and not on:energy+=d['startup_kwh']
        kwh+=energy;cost+=energy*price;thermal_kwh+=.25*thermal
        minimum=min(minimum,temperature);maximum=max(maximum,temperature)
        if not d['minimum_c']-1e-7<=temperature<=d['maximum_c']+1e-7:
            violations.append('temperature_'+str(i))
        on=command
    if temperature<d['initial_c']-1e-7:violations.append('terminal')
    return dict(cost_eur=cost,electric_kwh=kwh,thermal_kwh=thermal_kwh,minimum_c=minimum,
        maximum_c=maximum,final_c=temperature,starts=starts,violations=violations)

def solve(d, time_limit):
    n=d['periods'];dim=4*n+1
    temp=lambda t:t
    on=lambda t:n+1+t
    start=lambda t:2*n+1+t
    stop=lambda t:3*n+1+t
    c=np.zeros(dim)
    lower=np.zeros(dim);upper=np.ones(dim)
    lower[:n+1]=d['minimum_c'];upper[:n+1]=d['maximum_c']
    lower[0]=upper[0]=d['initial_c'];lower[n]=max(lower[n],d['initial_c'])
    integer=np.zeros(dim,dtype=int);integer[n+1:]=1
    # Honor the initial mode's remaining dwell time. The transition
    # constraints alone cover only changes initiated inside the horizon.
    initial_mode=int(bool(d['initial_on']))
    required=d['min_heat_min'] if initial_mode else d['min_off_min']
    remaining=max(0, required-d['initial_minutes'])
    locked_slots=min(n, math.ceil(remaining/15))
    for i in range(locked_slots):
        lower[on(i)]=upper[on(i)]=initial_mode
    rows=[];cols=[];values=[];lhs=[];rhs=[]
    def constrain(terms,low,high):
        row=len(lhs);lhs.append(low);rhs.append(high)
        for col,v in terms.items():
            if v:rows.append(row);cols.append(col);values.append(v)
    for i,slot in enumerate(d['slots']):
        outside,price,gain,derate=slot[:4]
        alpha=math.exp(-d['loss']*.25/d['mass'])
        beta=(1-alpha)/d['loss']
        heat=max(d['min_heat_kw'],d['heat_at_zero']+d['heat_slope']*outside)*derate
        cop=max(d['min_cop'],min(d['max_cop'],d['cop_at_zero']+d['cop_slope']*outside))
        delay=d['startup_delay_min']/15
        eq=(1-alpha)*outside+beta*gain
        constrain({temp(i+1):1,temp(i):-alpha,on(i):-beta*heat,
                   start(i):beta*heat*delay},eq,eq)
        c[on(i)]+=price*.25*(heat/cop+d['heat_standby_kw']-d['off_standby_kw'])
        c[start(i)]+=price*(d['startup_kwh']-.25*delay*heat/cop)+d['switch_penalty']
        c[stop(i)]+=d['switch_penalty']
        if i==0:
            constrain({on(i):1,start(i):-1,stop(i):1},
                      int(d['initial_on']),int(d['initial_on']))
        else:
            constrain({on(i):1,on(i-1):-1,start(i):-1,stop(i):1},0,0)
        constrain({start(i):1,stop(i):1},-np.inf,1)
        constrain({start(i):1,on(i):-1},-np.inf,0)
        constrain({stop(i):1,on(i):1},-np.inf,1)
        for j in range(i,min(n,i+d['min_heat_min']//15)):
            constrain({start(i):1,on(j):-1},-np.inf,0)
        for j in range(i,min(n,i+d['min_off_min']//15)):
            constrain({stop(i):1,on(j):1},-np.inf,1)
    matrix=coo_matrix((values,(rows,cols)),shape=(len(lhs),dim)).tocsr()
    begin=time.monotonic()
    sol=milp(c,integrality=integer,bounds=Bounds(lower,upper),
        constraints=LinearConstraint(matrix,lhs,rhs),
        options=dict(time_limit=time_limit,mip_rel_gap=.001))
    duration=round(time.monotonic()-begin,3)
    if sol.x is None:return dict(status=int(sol.status),seconds=duration,feasible=False)
    actions=np.rint(sol.x[n+1:2*n+1]).astype(int).tolist()
    outcome=replay(d,actions)
    outcome.update(status=int(sol.status),seconds=duration,
        mip_gap=float(sol.mip_gap) if sol.mip_gap is not None else None,
        feasible=not outcome['violations'],objective=float(sol.fun),actions=actions)
    return outcome

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--exe',type=Path)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--short',action='store_true')
    parser.add_argument('--time-limit',type=int,default=25)
    args=parser.parse_args()
    records=[]
    for case in ('mild','cold','price_spike','defrost'):
        for horizon in ([96] if args.short else [96,192]):
            d=inputs(args.exe,case,horizon)
            result=solve(d,args.time_limit)
            cpp=None
            if args.exe:
                cpp=replay(d,[point[4] for point in d['slots']])
                if cpp['violations']:raise ValueError('C++ schedule unsafe: '+str(cpp['violations']))
                if abs(cpp['cost_eur']-d['cost_eur'])>1e-7:
                    raise ValueError('C++ cost differs from independent replay')
            item=dict(case=case,slots=horizon,cpp=cpp,milp=result)
            if cpp and result['feasible']:
                item['cost_gap_eur']=cpp['cost_eur']-result['cost_eur']
                item['cost_gap_pct']=100*(cpp['cost_eur']/result['cost_eur']-1)
            records.append(item)
            print(case,horizon,'MILP cost',result.get('cost_eur'),
                  'status',result['status'],'C++',d.get('cost_eur'))
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(records,indent=2,allow_nan=False),encoding='utf-8')

if __name__=='__main__':main()
