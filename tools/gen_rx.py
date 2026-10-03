# -*- coding: utf-8 -*-
import io
# a, b, out, out2, t(от), tmax(до), p, heat, cat, eq
R = [
 # --- горение и оксиды ---
 ("H2","O2","H2O",None,560,9999,.35,300,None,"2H₂ + O₂ → 2H₂O"),
 ("C","O2","CO2",None,700,9999,.30,250,None,"C + O₂ → CO₂"),
 ("C","CO2","CO",None,900,9999,.20,-60,None,"C + CO₂ → 2CO"),
 ("CO","O2","CO2",None,610,9999,.35,200,None,"2CO + O₂ → 2CO₂"),
 ("S","O2","SO2",None,250,9999,.30,120,None,"S + O₂ → SO₂"),
 ("SO2","O2","SO3",None,420,9999,.15,60,"V","2SO₂ + O₂ → 2SO₃"),
 ("N2","O2","NO",None,1800,9999,.10,-40,None,"N₂ + O₂ → 2NO"),
 ("NO","O2","NO2",None,-200,9999,.40,30,None,"2NO + O₂ → 2NO₂"),
 ("Si","O2","SiO2",None,1000,9999,.25,200,None,"Si + O₂ → SiO₂"),
 ("P","O2","P2O5",None,60,9999,.40,250,None,"4P + 5O₂ → 2P₂O₅"),
 ("Fe","O2","Fe3O4",None,600,9999,.20,80,None,"3Fe + 2O₂ → Fe₃O₄"),
 ("Fe","H2O","Fe2O3","H2",22,200,.004,5,None,"4Fe + 6H₂O + 3O₂ → 4Fe(OH)₃ → 2Fe₂O₃"),
 ("Al","O2","Al2O3",None,660,9999,.30,300,None,"4Al + 3O₂ → 2Al₂O₃"),
 ("Mg","O2","MgO",None,473,9999,.45,320,None,"2Mg + O₂ → 2MgO"),
 ("Mg","CO2","MgO","C",600,9999,.25,180,None,"2Mg + CO₂ → 2MgO + C"),
 ("Ca","O2","CaO",None,300,9999,.30,200,None,"2Ca + O₂ → 2CaO"),
 ("Cu","O2","CuO",None,300,9999,.20,60,None,"2Cu + O₂ → 2CuO"),
 ("Zn","O2","ZnO",None,400,9999,.25,150,None,"2Zn + O₂ → 2ZnO"),
 ("Pb","O2","PbO",None,400,9999,.20,50,None,"2Pb + O₂ → 2PbO"),
 ("Ti","O2","TiO2",None,1200,9999,.20,200,None,"Ti + O₂ → TiO₂"),
 ("Na","O2","Na2O",None,120,9999,.30,100,None,"4Na + O₂ → 2Na₂O"),
 ("K","O2","K2O",None,60,9999,.35,120,None,"4K + O₂ → 2K₂O"),
 ("Al","Fe2O3","Al2O3","Fe",900,9999,.35,1500,None,"2Al + Fe₂O₃ → Al₂O₃ + 2Fe"),
 ("Si","C","SiC",None,2000,9999,.20,0,None,"Si + C → SiC"),
 ("Fe","C","Fe3C",None,1150,9999,.10,0,None,"3Fe + C → Fe₃C"),
 ("CaO","C","CaC2","CO",2000,9999,.20,-200,None,"CaO + 3C → CaC₂ + CO"),
 ("C","H2O","CO","H2",1000,9999,.25,-120,None,"C + H₂O → CO + H₂"),
 # --- азотная цепочка ---
 ("N2","H2","NH3",None,400,700,.20,40,"Fe","N₂ + 3H₂ ⇄ 2NH₃"),
 ("NH3","O2","NO","H2O",750,9999,.30,90,"Pt","4NH₃ + 5O₂ → 4NO + 6H₂O"),
 ("NO2","H2O","HNO3","NO",-50,9999,.30,20,None,"3NO₂ + H₂O → 2HNO₃ + NO"),
 ("N2O5","H2O","HNO3",None,-50,9999,.40,20,None,"N₂O₅ + H₂O → 2HNO₃"),
 # --- вода и оксиды ---
 ("Na","H2O","NaOH","H2",-50,9999,.90,160,None,"2Na + 2H₂O → 2NaOH + H₂"),
 ("K","H2O","KOH","H2",-50,9999,1.0,220,None,"2K + 2H₂O → 2KOH + H₂"),
 ("Ca","H2O","Ca(OH)2","H2",-50,9999,.30,60,None,"Ca + 2H₂O → Ca(OH)₂ + H₂"),
 ("CaO","H2O","Ca(OH)2",None,-50,9999,.60,120,None,"CaO + H₂O → Ca(OH)₂"),
 ("Na2O","H2O","NaOH",None,-50,9999,.60,90,None,"Na₂O + H₂O → 2NaOH"),
 ("K2O","H2O","KOH",None,-50,9999,.60,100,None,"K₂O + H₂O → 2KOH"),
 ("SO3","H2O","H2SO4",None,-50,9999,.50,100,None,"SO₃ + H₂O → H₂SO₄"),
 ("SO2","H2O","H2SO3",None,-50,9999,.25,20,None,"SO₂ + H₂O → H₂SO₃"),
 ("CO2","H2O","H2CO3",None,-50,9999,.05,5,None,"CO₂ + H₂O ⇄ H₂CO₃"),
 ("P2O5","H2O","H3PO4",None,-50,9999,.50,60,None,"P₂O₅ + 3H₂O → 2H₃PO₄"),
 ("CaC2","H2O","C2H2","Ca(OH)2",-50,9999,.70,40,None,"CaC₂ + 2H₂O → C₂H₂ + Ca(OH)₂"),
 ("NH3","H2O","NH4OH",None,-50,80,.35,10,None,"NH₃ + H₂O ⇄ NH₄OH"),
 ("Cl2","H2O","HCl",None,-50,9999,.10,5,None,"Cl₂ + H₂O ⇄ HCl + HClO"),
 # --- галогены с металлами ---
 ("Cl2","Na","NaCl",None,-50,9999,.80,200,None,"2Na + Cl₂ → 2NaCl"),
 ("Cl2","K","KCl",None,-50,9999,.80,210,None,"2K + Cl₂ → 2KCl"),
 ("Cl2","Ca","CaCl2",None,-50,9999,.50,190,None,"Ca + Cl₂ → CaCl₂"),
 ("Cl2","Mg","MgCl2",None,100,9999,.50,180,None,"Mg + Cl₂ → MgCl₂"),
 ("Cl2","Fe","FeCl3",None,200,9999,.40,120,None,"2Fe + 3Cl₂ → 2FeCl₃"),
 ("Cl2","Cu","CuCl2",None,200,9999,.35,100,None,"Cu + Cl₂ → CuCl₂"),
 ("Cl2","Ag","AgCl",None,100,9999,.30,80,None,"2Ag + Cl₂ → 2AgCl"),
 ("Br2","Na","NaBr",None,-50,9999,.60,170,None,"2Na + Br₂ → 2NaBr"),
 ("I2","Na","NaI",None,-50,9999,.40,140,None,"2Na + I₂ → 2NaI"),
 ("Cl2","H2","HCl",None,200,9999,.50,90,None,"H₂ + Cl₂ → 2HCl"),
 ("S","H2","H2S",None,300,9999,.30,20,None,"H₂ + S → H₂S"),
 # --- кислоты с металлами ---
 ("HCl","Na","NaCl","H2",-50,9999,.80,120,None,"2Na + 2HCl → 2NaCl + H₂"),
 ("HCl","K","KCl","H2",-50,9999,.80,130,None,"2K + 2HCl → 2KCl + H₂"),
 ("HCl","Mg","MgCl2","H2",-50,9999,.50,80,None,"Mg + 2HCl → MgCl₂ + H₂"),
 ("HCl","Ca","CaCl2","H2",-50,9999,.50,90,None,"Ca + 2HCl → CaCl₂ + H₂"),
 ("HCl","Fe","FeCl3","H2",-50,9999,.25,40,None,"2Fe + 6HCl → 2FeCl₃ + 3H₂"),
 ("H2SO4","Fe","FeSO4","H2",-50,9999,.25,40,None,"Fe + H₂SO₄ → FeSO₄ + H₂"),
 ("H2SO4","Zn","ZnSO4","H2",-50,9999,.30,50,None,"Zn + H₂SO₄ → ZnSO₄ + H₂"),
 ("H2SO4","Mg","MgSO4","H2",-50,9999,.35,60,None,"Mg + H₂SO₄ → MgSO₄ + H₂"),
 ("H2SO4","Cu","CuSO4",None,100,9999,.20,30,None,"Cu + 2H₂SO₄ → CuSO₄ + SO₂ + 2H₂O"),
 ("H2SO4","Pb","PbSO4",None,-50,9999,.15,20,None,"Pb + H₂SO₄ → PbSO₄ + H₂"),
 ("HI","Pb","PbI2","H2",-50,9999,.25,20,None,"Pb + 2HI → PbI₂ + H₂"),
 # --- нейтрализация ---
 ("HCl","NaOH","NaCl","H2O",-50,9999,.70,55,None,"HCl + NaOH → NaCl + H₂O"),
 ("HCl","KOH","KCl","H2O",-50,9999,.70,55,None,"HCl + KOH → KCl + H₂O"),
 ("HCl","NH3","NH4Cl",None,-50,9999,.60,45,None,"HCl + NH₃ → NH₄Cl"),
 ("HNO3","NaOH","NaNO3","H2O",-50,9999,.70,55,None,"HNO₃ + NaOH → NaNO₃ + H₂O"),
 ("HNO3","NH3","NH4NO3",None,-50,9999,.60,50,None,"HNO₃ + NH₃ → NH₄NO₃"),
 ("H2SO4","NaOH","Na2SO4","H2O",-50,9999,.70,60,None,"H₂SO₄ + 2NaOH → Na₂SO₄ + 2H₂O"),
 ("H2SO4","Ca(OH)2","CaSO4","H2O",-50,9999,.60,55,None,"H₂SO₄ + Ca(OH)₂ → CaSO₄ + 2H₂O"),
 # --- соли ---
 ("HCl","CaCO3","CaCl2","CO2",-50,9999,.50,10,None,"CaCO₃ + 2HCl → CaCl₂ + H₂O + CO₂"),
 ("HCl","NaHCO3","NaCl","CO2",-50,9999,.60,8,None,"NaHCO₃ + HCl → NaCl + H₂O + CO₂"),
 ("HCl","Na2CO3","NaCl","CO2",-50,9999,.55,10,None,"Na₂CO₃ + 2HCl → 2NaCl + H₂O + CO₂"),
 ("H2SO4","CaCO3","CaSO4","CO2",-50,9999,.45,12,None,"CaCO₃ + H₂SO₄ → CaSO₄ + H₂O + CO₂"),
 ("H2SO4","NaCl","HCl","Na2SO4",150,9999,.30,0,None,"2NaCl + H₂SO₄ → Na₂SO₄ + 2HCl"),
 ("CuSO4","Fe","FeSO4","Cu",-50,9999,.35,15,None,"Fe + CuSO₄ → FeSO₄ + Cu"),
 ("AgNO3","NaCl","AgCl","NaNO3",-50,9999,.60,0,None,"AgNO₃ + NaCl → AgCl↓ + NaNO₃"),
 ("Na2CO3","SiO2","Na2SiO3","CO2",1000,9999,.25,-50,None,"Na₂CO₃ + SiO₂ → Na₂SiO₃ + CO₂"),
 ("CaO","SiO2","CaSiO3" if False else "Na2SiO3",None,1300,9999,.10,0,None,"CaO + SiO₂ → CaSiO₃"),
 ("CO","H2","CH3OH",None,250,500,.20,30,"Cu","CO + 2H₂ → CH₃OH"),
 ("H2SO4","C12H22O11","C","H2O",-50,9999,.60,80,None,"C₁₂H₂₂O₁₁ →(H₂SO₄) 12C + 11H₂O"),
 ("C6H12O6","H2O","C2H5OH","CO2",18,45,.03,5,None,"C₆H₁₂O₆ → 2C₂H₅OH + 2CO₂"),
]
# разложение при нагреве: a, out, out2, t, p, eq
D = [
 ("CaCO3","CaO","CO2",825,.30,"CaCO₃ → CaO + CO₂"),
 ("NaHCO3","Na2CO3","CO2",80,.30,"2NaHCO₃ → Na₂CO₃ + H₂O + CO₂"),
 ("KMnO4","MnO2","O2",240,.30,"2KMnO₄ → K₂MnO₄ + MnO₂ + O₂"),
 ("NH4NO3","N2O","H2O",210,.40,"NH₄NO₃ → N₂O + 2H₂O"),
 ("H2O2","H2O","O2",60,.25,"2H₂O₂ → 2H₂O + O₂"),
 ("Cu(OH)2","CuO","H2O",80,.40,"Cu(OH)₂ → CuO + H₂O"),
 ("Fe(OH)3","Fe2O3","H2O",135,.40,"2Fe(OH)₃ → Fe₂O₃ + 3H₂O"),
 ("Mg(OH)2","MgO","H2O",350,.40,"Mg(OH)₂ → MgO + H₂O"),
 ("Al(OH)3","Al2O3","H2O",300,.40,"2Al(OH)₃ → Al₂O₃ + 3H₂O"),
 ("Ca(OH)2","CaO","H2O",580,.35,"Ca(OH)₂ → CaO + H₂O"),
 ("H2CO3","CO2","H2O",30,.35,"H₂CO₃ → CO₂ + H₂O"),
 ("NH4OH","NH3","H2O",38,.35,"NH₄OH → NH₃ + H₂O"),
 ("NH4Cl","NH3","HCl",338,.35,"NH₄Cl → NH₃ + HCl"),
 ("N2O5","NO2","O2",47,.30,"2N₂O₅ → 4NO₂ + O₂"),
 ("MnO2","MnO2","O2",535,.05,"2MnO₂ → 2MnO + O₂"),
]
def q(v): return "nil" if v is None else '"%s"' % v
o = io.StringIO()
o.write("Cp.rx = {\n")
for a,b,out,out2,t,tmax,p,heat,cat,eq in R:
    o.write('{a=%s, b=%s, out=%s, out2=%s, t=%g, tmax=%g, p=%g, heat=%g, cat=%s, eq="%s"},\n'
            % (q(a),q(b),q(out),q(out2),t,tmax,p,heat,q(cat),eq))
o.write("}\n\nCp.dec = {\n")
for a,out,out2,t,p,eq in D:
    o.write('{a=%s, out=%s, out2=%s, t=%g, p=%g, eq="%s"},\n' % (q(a),q(out),q(out2),t,p,eq))
o.write("}\n\nreturn Cp\n")
open('/home/user/sandbox/tools/_part2.lua','w').write(o.getvalue())
print("реакций:", len(R), " разложений:", len(D))
