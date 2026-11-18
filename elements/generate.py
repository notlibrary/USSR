"""Builds the KGB elements base (the folders next to this file) from the
mendeleev Python package, plus the hand-written descriptions in
descriptions.txt.

    python -m venv venv && venv/Scripts/pip install mendeleev==1.3.0
    venv/Scripts/python elements/generate.py

It removes the element folders it made before (the category folders listed in
CATEGORIES) and writes them again, so running it twice gives the same files.
See README.md for the fields, their units and their sources.
"""
import math
import os
import shutil

import mendeleev
from mendeleev import element
from mendeleev.fetch import fetch_table

HERE = os.path.dirname(os.path.abspath(__file__))

# mendeleev series -> category path in the base and the category as a phrase
CATEGORIES = {
    "Alkali metals": ("metals/alkali-metals", "alkali metal"),
    "Alkaline earth metals": ("metals/alkaline-earth-metals", "alkaline earth metal"),
    "Transition metals": ("metals/transition-metals", "transition metal"),
    "Poor metals": ("metals/post-transition-metals", "post-transition metal"),
    "Lanthanides": ("metals/lanthanides", "lanthanide"),
    "Actinides": ("metals/actinides", "actinide"),
    "Metalloids": ("metalloids", "metalloid"),
    "Nonmetals": ("nonmetals/reactive-nonmetals", "reactive nonmetal"),
    "Halogens": ("nonmetals/halogens", "halogen"),
    "Noble gases": ("nonmetals/noble-gases", "noble gas"),
}
TOPS = ["metals", "metalloids", "nonmetals"]

LATTICE = {
    "BCC": "body-centred cubic",
    "FCC": "face-centred cubic",
    "HEX": "hexagonal",
    "ORC": "orthorhombic",
    "RHL": "rhombohedral",
    "TET": "tetragonal",
    "DIA": "diamond cubic",
    "CUB": "cubic",
    "MCL": "monoclinic",
    "SC": "simple cubic",
}

# half-life units of NUBASE2020 (as mendeleev stores them) in seconds
YEAR = 365.2422 * 86400
HALF_LIFE_S = {
    "asec": 1e-18, "zsec": 1e-21, "ysec": 1e-24, "psec": 1e-12, "nsec": 1e-9,
    "usec": 1e-6, "msec": 1e-3, "sec": 1.0, "minute": 60.0, "hour": 3600.0,
    "day": 86400.0, "year": YEAR, "kyear": 1e3 * YEAR, "Myear": 1e6 * YEAR,
    "Gyear": 1e9 * YEAR, "Tyear": 1e12 * YEAR, "Pyear": 1e15 * YEAR,
    "Eyear": 1e18 * YEAR, "Zyear": 1e21 * YEAR, "Yyear": 1e24 * YEAR,
}
HALF_LIFE_WORD = {
    "asec": "as", "zsec": "zs", "ysec": "ys", "psec": "ps", "nsec": "ns",
    "usec": "µs", "msec": "ms", "sec": "s", "minute": "min", "hour": "h",
    "day": "d", "year": "years", "kyear": "thousand years",
    "Myear": "million years", "Gyear": "billion years",
    "Tyear": "× 10^12 years", "Pyear": "× 10^15 years",
    "Eyear": "× 10^18 years", "Zyear": "× 10^21 years",
    "Yyear": "× 10^24 years",
}

# No weighable sample of these has ever been made, so the bulk properties
# mendeleev lists for them (melting and boiling points, density, crystal
# structure, heat data) are estimates or predictions, not measurements.
# Astatine and francium are in the same situation.
BULK_PREDICTED = set(range(100, 119)) | {85, 87}
# For the superheavy elements the atomic properties (radii, electron
# affinity, electronegativity) are computed, not measured.
ATOMIC_PREDICTED = set(range(104, 119))
# Category assigned from the position in the table, chemistry not (or barely)
# studied.
CATEGORY_PREDICTED = set(range(109, 119))

UNKNOWN = "unknown"


def num(x, digits=None):
    """A number as text: integers without .0, others trimmed."""
    if x is None or (isinstance(x, float) and math.isnan(x)):
        return None
    if digits is not None:
        x = round(float(x), digits)
    if float(x) == int(x) and abs(x) < 1e15:
        return str(int(x))
    s = "%.10g" % x
    return s


def sig(x, n=4):
    """x to n significant figures, plain notation when reasonable."""
    if x == 0:
        return "0"
    s = "%.*g" % (n, x)
    if "e" in s:
        m, e = s.split("e")
        return "%se%d" % (m, int(e))
    return s


def missing(x):
    return x is None or (isinstance(x, float) and math.isnan(x)) or x == ""


class Out:
    def __init__(self):
        self.lines = []

    def h(self, title):
        if self.lines and self.lines[-1] != "":
            self.lines.append("")
        self.lines.append("## " + title)
        self.lines.append("")

    def note(self, text):
        self.lines.append(text)

    def fact(self, key, value):
        self.lines.append("- %s: %s" % (key, value))

    def prop(self, key, value, predicted=False):
        """A measured value, or `unknown` plus key_predicted when the value
        is an estimate, or `unknown` when there is none."""
        if missing(value):
            self.fact(key, UNKNOWN)
        elif predicted:
            self.fact(key, UNKNOWN)
            self.fact(key + "_predicted", value)
        else:
            self.fact(key, value)


def load_descriptions():
    d = {}
    with open(os.path.join(HERE, "descriptions.txt"), encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            z, text = line.split("|", 1)
            d[int(z)] = text.strip()
    return d


def isotope_facts(e):
    """(natural isotopes text, stable count, most stable text, half-life s)"""
    natural = []
    stable = []
    longest = None
    for i in e.isotopes:
        if i.abundance:
            natural.append((i.mass_number, i.abundance))
        # stable: found in nature and no measured half-life (NUBASE lists
        # unmeasured synthetic isotopes without a half-life too)
        if i.abundance and i.half_life is None:
            stable.append(i.mass_number)
            continue
        if i.half_life is None or i.half_life_unit not in HALF_LIFE_S:
            continue
        s = i.half_life * HALF_LIFE_S[i.half_life_unit]
        if longest is None or s > longest[0]:
            longest = (s, i)
    nat = ", ".join("%s-%d %s%%" % (e.symbol, m, num(a, 4)) for m, a in natural)
    if stable:
        # the most abundant stable isotope
        best = max(stable, key=lambda m: dict(natural).get(m, 0))
        most = "%s-%d (stable)" % (e.symbol, best)
        return nat, len(stable), most, None, longest
    if longest is None:
        return nat, 0, None, None, None
    s, i = longest
    unit = HALF_LIFE_WORD[i.half_life_unit]
    hl = num(i.half_life, 6)
    if unit.startswith("×"):
        text = "%s-%d, half-life %s %s" % (e.symbol, i.mass_number, hl, unit)
    else:
        text = "%s-%d, half-life %s %s" % (e.symbol, i.mass_number, hl, unit)
    return nat, 0, text, s, longest


def main():
    print("mendeleev", mendeleev.__version__)
    desc = load_descriptions()
    phases = fetch_table("phasetransitions")
    ies = fetch_table("ionizationenergies")
    oxs = fetch_table("oxidationstates")

    for top in TOPS:
        p = os.path.join(HERE, top)
        if os.path.isdir(p):
            shutil.rmtree(p)

    write_category_files()

    for z in range(1, 119):
        e = element(z)
        cat_path, cat_word = CATEGORIES[e.series]
        folder = e.name.lower()
        d = os.path.join(HERE, *cat_path.split("/"), folder)
        os.makedirs(d)
        text = element_md(e, cat_word, desc.get(z), phases, ies, oxs)
        with open(os.path.join(d, folder + ".md"), "w", encoding="utf-8",
                  newline="\n") as f:
            f.write(text)
    print("118 elements written")


def write_category_files():
    files = {
        "metals": ("Metals",
                   ["metal: yes",
                    "data_source: see the README.md at the root of this base"],
                   "Elements that are metals: lustrous, conducting, mostly "
                   "solid, forming cations."),
        "metalloids": ("Metalloids",
                       ["metal: borderline", "series: metalloids",
                        "data_source: see the README.md at the root of this base"],
                       "Elements with properties between metals and nonmetals."),
        "nonmetals": ("Nonmetals",
                      ["metal: no",
                       "data_source: see the README.md at the root of this base"],
                      "Elements that are not metals."),
        "metals/alkali-metals": ("Alkali metals", ["series: alkali metals"],
                                 "Group 1 apart from hydrogen."),
        "metals/alkaline-earth-metals": ("Alkaline earth metals",
                                         ["series: alkaline earth metals"],
                                         "Group 2."),
        "metals/transition-metals": ("Transition metals",
                                     ["series: transition metals"],
                                     "The d-block, groups 3 to 12."),
        "metals/post-transition-metals": ("Post-transition metals",
                                          ["series: post-transition metals"],
                                          "The metals of the p-block "
                                          "(\"poor metals\")."),
        "metals/lanthanides": ("Lanthanides", ["series: lanthanides"],
                               "Lanthanum to lutetium."),
        "metals/actinides": ("Actinides", ["series: actinides", "radioactive: yes"],
                             "Actinium to lawrencium; all radioactive."),
        "nonmetals/reactive-nonmetals": ("Reactive nonmetals",
                                         ["series: reactive nonmetals"],
                                         "Hydrogen, carbon, nitrogen, oxygen, "
                                         "phosphorus, sulfur and selenium."),
        "nonmetals/halogens": ("Halogens", ["series: halogens"], "Group 17."),
        "nonmetals/noble-gases": ("Noble gases", ["series: noble gases"],
                                  "Group 18."),
    }
    schema = [
        "!atomic_number: int",
        "!symbol: text",
        "!atomic_mass_u: real",
        "!period: int",
    ]
    for path, (title, facts, note) in files.items():
        d = os.path.join(HERE, *path.split("/"))
        os.makedirs(d, exist_ok=True)
        name = path.split("/")[-1]
        lines = ["# " + title, "", note, ""]
        for f in facts:
            lines.append("- " + f)
        if "/" not in path:
            lines.append("")
            lines.append("## Schema")
            lines.append("")
            lines.append("Required, typed keys of every element below "
                         "(`kgb validate`).")
            lines.append("")
            for f in schema:
                lines.append("- " + f)
        with open(os.path.join(d, name + ".md"), "w", encoding="utf-8",
                  newline="\n") as f:
            f.write("\n".join(lines) + "\n")


def element_md(e, cat_word, description, phases, ies, oxs):
    z = e.atomic_number
    o = Out()
    o.note("# %s (%s)" % (e.name, e.symbol))
    o.note("")
    if description:
        o.note(description)
        o.note("")
    o.fact("atomic_number", z)
    o.fact("symbol", e.symbol)
    o.fact("name", e.name)
    o.fact("aka", e.symbol)
    # standard atomic weight, or the mass number of the longest-lived isotope
    if e.atomic_weight_uncertainty is not None:
        o.fact("atomic_mass_u", num(e.atomic_weight))
        o.fact("atomic_mass_uncertainty_u", num(e.atomic_weight_uncertainty))
    else:
        o.fact("atomic_mass_u", num(e.atomic_weight))
        o.fact("atomic_mass_note",
               "no standard atomic weight; mass number of the longest-lived "
               "isotope")
    o.fact("group", e.group_id if e.group_id else "none (f-block)")
    o.fact("period", e.period)
    o.fact("block", e.block)
    o.fact("category", cat_word)
    if z in CATEGORY_PREDICTED:
        o.fact("category_basis", "predicted from its place in the table")
    if e.cas:
        o.fact("cas_number", e.cas)

    o.h("Atomic structure")
    # mendeleev writes a lone electron as "4s"; spell it "4s1"
    econf = " ".join(t + "1" if t[-1] in "spdfg" and t[0].isdigit() else t
                     for t in e.econf.split())
    o.fact("electron_configuration", econf)
    shells = e.ec.electrons_per_shell()
    o.fact("electrons_per_shell", ", ".join(str(v) for v in shells.values()))
    o.prop("electronegativity_pauling", num(e.en_pauling),
           z in ATOMIC_PREDICTED)
    ie = ies[(ies.atomic_number == z)].sort_values("ion_charge")
    ie1 = ie[ie.ion_charge == 0]
    if len(ie1) and not missing(ie1.ionization_energy.iloc[0]):
        v = float(ie1.ionization_energy.iloc[0])
        theo = bool(ie1.is_theoretical.iloc[0]) or bool(
            ie1.is_semi_empirical.iloc[0])
        o.prop("ionization_energy_1_eV", num(v, 4), theo)
        o.prop("ionization_energy_1_kJ_mol", num(v * 96.48533212, 1), theo)
    else:
        o.fact("ionization_energy_1_eV", UNKNOWN)
    vals = []
    for _, r in ie.iterrows():
        if missing(r.ionization_energy):
            continue
        t = num(r.ionization_energy, 3)
        if r.is_theoretical or r.is_semi_empirical:
            t += "*"
        vals.append(t)
    if vals:
        o.fact("ionization_energies_eV", ", ".join(vals))
        if any(v.endswith("*") for v in vals):
            o.fact("ionization_energies_note",
                   "* marks a calculated or semi-empirical value (NIST ASD)")
    ea = e.electron_affinity
    if not missing(ea):
        o.prop("electron_affinity_eV", num(ea, 4), z in ATOMIC_PREDICTED)
        o.prop("electron_affinity_kJ_mol", num(ea * 96.48533212, 1),
               z in ATOMIC_PREDICTED)
    else:
        o.fact("electron_affinity_eV", UNKNOWN)
    o.prop("atomic_radius_pm", num(e.atomic_radius), z in ATOMIC_PREDICTED)
    o.prop("covalent_radius_pm", num(e.covalent_radius_cordero),
           z in ATOMIC_PREDICTED)
    # Pyykko's radii are fitted and calculated for every element, so they
    # stay a separate field, not a measurement
    if not missing(e.covalent_radius_pyykko):
        o.fact("covalent_radius_pyykko_pm", num(e.covalent_radius_pyykko, 1))
    o.prop("vdw_radius_pm", num(e.vdw_radius), z in ATOMIC_PREDICTED)
    o.prop("metallic_radius_pm", num(e.metallic_radius), z in ATOMIC_PREDICTED)
    ox = oxs[oxs.atomic_number == z]
    main_ox = sorted(int(v) for v in ox[ox.category == "main"].oxidation_state)
    all_ox = sorted(int(v) for v in ox.oxidation_state)

    def fmt(vs):
        return ", ".join(("+%d" % v) if v > 0 else str(v) for v in vs)
    if main_ox:
        o.fact("oxidation_states", fmt(main_ox))
        if all_ox != main_ox:
            o.fact("oxidation_states_all", fmt(all_ox))
    elif all_ox:
        # only predicted states (the superheavy elements)
        o.fact("oxidation_states", UNKNOWN)
        o.fact("oxidation_states_predicted", fmt(all_ox))
    else:
        o.fact("oxidation_states", UNKNOWN)

    o.h("Physical properties")
    bulk = z in BULK_PREDICTED
    T = 273.15  # phase at STP (273.15 K, 100 kPa) from mp and bp
    ph = phases[phases.atomic_number == z]
    mp = e.melting_point
    bp = e.boiling_point
    # elements with several allotropes (P, S, Se, Sn) have one row each in
    # the CRC data; the main melting point is that of the form that melts
    # (not a solid-solid transition), and for phosphorus the white form,
    # its reference state
    row = None
    if len(ph) > 1:
        if z == 15:
            row = ph[ph.allotrope == "white"].iloc[0]
        else:
            row = ph[ph.is_transition != 1].iloc[0]
        mp = row.melting_point
        bp = row.boiling_point
    elif len(ph) == 1:
        row = ph.iloc[0]
    o.prop("melting_point_K", num(mp, 2), bulk)
    if not missing(mp):
        o.prop("melting_point_C", num(mp - 273.15, 2), bulk)
    o.prop("boiling_point_K", num(bp, 2), bulk)
    if not missing(bp):
        o.prop("boiling_point_C", num(bp - 273.15, 2), bulk)
    if row is not None and row.is_sublimation_point == 1:
        o.fact("sublimes", "yes, at normal pressure (boiling point is the "
               "sublimation point)")
    if z == 2:
        o.fact("melting_point_note",
               "does not solidify at normal pressure (needs about 2.5 MPa)")
    if len(ph) > 1:
        for _, a in ph.iterrows():
            parts = []
            if not missing(a.melting_point):
                parts.append(("transition to another form at %s K"
                              if a.is_transition == 1 else
                              "melting point %s K") % num(a.melting_point, 2))
            if not missing(a.boiling_point):
                parts.append(("sublimes at %s K" if a.is_sublimation_point == 1
                              else "boiling point %s K")
                             % num(a.boiling_point, 2))
            o.fact("allotrope", "%s: %s" % (a.allotrope, ", ".join(parts)))
    for _, r in ([] if row is None else [(0, row)]):
        if r.allotrope and len(ph) == 1:
            o.fact("phase_data_allotrope", r.allotrope)
        if not missing(r.critical_temperature):
            o.fact("critical_temperature_K", num(r.critical_temperature, 2))
        if not missing(r.critical_pressure):
            o.fact("critical_pressure_MPa", num(r.critical_pressure, 3))
        if not missing(r.triple_point_temperature):
            o.fact("triple_point_K", num(r.triple_point_temperature, 3))
        if not missing(r.triple_point_pressure):
            o.fact("triple_point_kPa", num(r.triple_point_pressure, 3))
        break
    if bulk:
        o.fact("phase_at_stp", UNKNOWN)
    elif not missing(bp) and bp < T:
        o.fact("phase_at_stp", "gas")
    elif not missing(mp) and mp < T:
        o.fact("phase_at_stp", "liquid")
    elif missing(mp) and missing(bp):
        o.fact("phase_at_stp", UNKNOWN)
    else:
        o.fact("phase_at_stp", "solid")
    o.prop("density_g_cm3", num(e.density, 5), bulk)
    if not missing(e.density) and not bulk and e.density < 0.02:
        o.fact("density_note", "gas density at 273.15 K and 101.325 kPa")
    crys = LATTICE.get(e.lattice_structure) if e.lattice_structure else None
    o.prop("crystal_structure", crys, bulk)
    if crys and not bulk and (mp is None or mp < T):
        o.fact("crystal_structure_note", "of the solid")
    o.prop("lattice_constant_angstrom", num(e.lattice_constant, 4), bulk)
    o.prop("thermal_conductivity_W_mK", num(e.thermal_conductivity, 4), bulk)
    o.prop("specific_heat_J_gK", num(e.specific_heat_capacity, 4), bulk)
    o.prop("molar_heat_capacity_J_molK", num(e.molar_heat_capacity, 4), bulk)
    o.prop("heat_of_fusion_kJ_mol", num(e.fusion_heat, 4), bulk)
    o.prop("heat_of_vaporization_kJ_mol", num(e.evaporation_heat, 4), bulk)

    o.h("Occurrence and isotopes")
    o.fact("radioactive", "yes" if e.is_radioactive else "no")
    nat, nstable, most, hl_s, _ = isotope_facts(e)
    o.fact("stable_isotopes", nstable)
    o.fact("natural_isotopes", nat if nat else "none")
    o.fact("most_stable_isotope", most if most else UNKNOWN)
    if hl_s is not None:
        o.fact("most_stable_half_life_s", sig(hl_s, 4))
    o.fact("abundance_crust_mg_kg",
           num(e.abundance_crust) if not missing(e.abundance_crust)
           else UNKNOWN)
    o.fact("abundance_sea_mg_L",
           num(e.abundance_sea) if not missing(e.abundance_sea) else UNKNOWN)
    if e.goldschmidt_class:
        o.fact("goldschmidt_class", e.goldschmidt_class)
    if e.geochemical_class:
        o.fact("geochemical_class", e.geochemical_class)

    o.h("History and name")
    if e.discovery_year:
        o.fact("discovery_year", e.discovery_year)
    else:
        o.fact("discovery_year", "antiquity")
    if e.discoverers:
        o.fact("discoverers", e.discoverers.strip().rstrip("."))
    if e.discovery_location:
        o.fact("discovery_location", e.discovery_location.strip())
    if e.name_origin:
        o.fact("name_origin", e.name_origin.strip().rstrip("."))

    o.h("Colours")
    o.fact("cpk_color", e.cpk_color if e.cpk_color else UNKNOWN)
    if e.jmol_color:
        o.fact("jmol_color", e.jmol_color)
    return "\n".join(o.lines) + "\n"


if __name__ == "__main__":
    main()
