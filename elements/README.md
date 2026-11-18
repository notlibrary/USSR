# Elements

All 118 chemical elements as a [KGB](../src/kgb/README.md) knowledge base:
one folder per element, classified by kind, with about 55 facts each.

```
elements/
  .kgb                         marks the root of the base
  metals/                      metals.md: metal: yes, data_source, schema
    alkali-metals/             alkali-metals.md: series: alkali metals
      sodium/sodium.md         the facts about sodium
    alkaline-earth-metals/  transition-metals/  post-transition-metals/
    lanthanides/  actinides/
  metalloids/                  boron, silicon, germanium, arsenic, ...
  nonmetals/
    reactive-nonmetals/  halogens/  noble-gases/
  generate.py                  rebuilds the element folders
  descriptions.txt             the one-line descriptions
```

The classification follows the `series` of the mendeleev package (below),
so polonium is a metalloid and lanthanum and actinium are lanthanide and
actinide. Elements 109 to 118 are placed by their position in the table,
not by measured chemistry, and say so with `category_basis`.

## Queries

From the repository root (`kgb` is `ussr src/kgb/kgb.su`; inside `elements/`
the `--kb` option can be left out, the `.kgb` file is found like `.git`):

```bash
kgb --kb elements show Fe                       # by symbol (the aka fact) or name
kgb --kb elements get gold density_g_cm3
kgb --kb elements type Na                       # metals > alkali-metals > sodium
kgb --kb elements where group=17 sort by atomic_number show symbol,phase_at_stp
kgb --kb elements where "melting_point_K>3000" sort by melting_point_K desc
kgb --kb elements where "electronegativity_pauling>=3" show symbol
kgb --kb elements where phase_at_stp=liquid
kgb --kb elements where "radioactive=yes and atomic_number<84"
kgb --kb elements where discovery_year=antiquity
kgb --kb elements where atomic_number group by series
kgb --kb elements --format csv where atomic_number sort by atomic_number show symbol,name,atomic_mass_u
kgb --kb elements sql "select symbol, name from nonmetals where atomic_number < 10"
kgb --kb elements table halogens symbol boiling_point_K
```

`tests/kgb_elements_test.su` runs these kinds of queries and compares the
output to `tests/expected/kgb_elements_test.su.txt`:

```bash
ussr tests/kgb_elements_test.su
```

## Fields

Numbers are plain numbers so that `where` can compare them. The unit is the
last part of the key: `_K` kelvin, `_C` degrees Celsius, `_pm` picometres,
`_eV` electronvolts, `_kJ_mol` kJ/mol, `_g_cm3` g/cm³, `_mg_kg` mg/kg,
`_u` daltons. kgb matches keys ignoring case.

| Key | Meaning |
|-----|---------|
| `atomic_number`, `symbol`, `name` | |
| `aka` | the symbol, so `kgb show Fe` finds iron |
| `atomic_mass_u` | standard atomic weight (u); for elements without one, the mass number of the longest-lived isotope and `atomic_mass_note` |
| `atomic_mass_uncertainty_u` | its uncertainty |
| `group`, `period`, `block` | `group` is `none (f-block)` for most of the f-block |
| `category`, `series` | the kind of element; `series` is inherited from the category file |
| `metal` | `yes`, `borderline` or `no`, inherited from the top category |
| `category_basis` | for 109 to 118: placed from the position in the table |
| `cas_number` | CAS registry number |
| `electron_configuration` | ground state, noble gas core in brackets |
| `electrons_per_shell` | K, L, M, ... shells |
| `electronegativity_pauling` | Pauling scale |
| `ionization_energy_1_eV`, `ionization_energy_1_kJ_mol` | first ionisation energy |
| `ionization_energies_eV` | all successive ionisation energies; `*` marks a calculated or semi-empirical value |
| `electron_affinity_eV`, `electron_affinity_kJ_mol` | |
| `atomic_radius_pm` | empirical atomic radius (Slater) |
| `covalent_radius_pm` | single-bond covalent radius (Cordero et al.) |
| `covalent_radius_pyykko_pm` | single-bond covalent radius (Pyykkö and Atsumi), fitted and calculated, given for every element |
| `vdw_radius_pm` | van der Waals radius |
| `metallic_radius_pm` | metallic radius (12-coordinate where known) |
| `oxidation_states` | the main (common) oxidation states |
| `oxidation_states_all` | every known oxidation state, rare ones included |
| `melting_point_K`, `melting_point_C`, `boiling_point_K`, `boiling_point_C` | at 101.325 kPa |
| `sublimes` | the boiling point is a sublimation point (carbon, arsenic) |
| `allotrope` | melting and boiling data of each form, for P, S, Se and Sn; the main values are the form that melts (white phosphorus) |
| `phase_data_allotrope` | the form the phase data are for, when the source names one |
| `critical_temperature_K`, `critical_pressure_MPa`, `triple_point_K`, `triple_point_kPa` | where known |
| `phase_at_stp` | `solid`, `liquid` or `gas` at 273.15 K and 100 kPa, worked out from the melting and boiling points |
| `density_g_cm3` | near room temperature; for gases at 273.15 K and 101.325 kPa (`density_note`) |
| `crystal_structure`, `lattice_constant_angstrom` | of the solid (`crystal_structure_note` for gases and liquids) |
| `thermal_conductivity_W_mK`, `specific_heat_J_gK`, `molar_heat_capacity_J_molK` | near 300 K |
| `heat_of_fusion_kJ_mol`, `heat_of_vaporization_kJ_mol` | |
| `radioactive` | `yes` when no isotope is stable |
| `stable_isotopes` | how many isotopes are stable |
| `natural_isotopes` | isotopes found in nature and their abundance in atom % |
| `most_stable_isotope` | the most abundant stable isotope, or the longest-lived one and its half-life |
| `most_stable_half_life_s` | that half-life in seconds, for radioactive elements |
| `abundance_crust_mg_kg`, `abundance_sea_mg_L` | in the Earth's crust and in sea water |
| `goldschmidt_class`, `geochemical_class` | geochemical classification |
| `discovery_year` | a year, or `antiquity` |
| `discoverers`, `discovery_location`, `name_origin` | |
| `cpk_color`, `jmol_color` | colours of the element in molecule models |

The first paragraph under each element's title is a one-line description,
written for this base (`descriptions.txt`).

### Unknown and predicted values

A value is never guessed:

- `unknown` means the sources have no value (for example the melting point
  of helium at normal pressure, or the crystal structure of seaborgium).
  `unknown` is not a number, so `where "melting_point_K>1000"` skips it, and
  `where melting_point_K=unknown` lists the gaps.
- For values that are estimates or calculations, not measurements, the key
  is `unknown` and the estimate is under the same key with `_predicted`, for
  example `density_g_cm3: unknown` and `density_g_cm3_predicted: 9.7` for
  fermium. This applies to:
  - the bulk properties (melting and boiling points, density, crystal
    structure, heat data, phase) of astatine, francium and elements 100 to
    118, of which no visible amount has ever been made;
  - the atomic properties (radii, electronegativity, electron affinity) of
    elements 104 to 118;
  - first ionisation energies that NIST lists as calculated or
    semi-empirical;
  - oxidation states of the superheavy elements that are only predicted
    (`oxidation_states_predicted`).

## Sources

All values except the descriptions come from the
[mendeleev](https://github.com/lmmentel/mendeleev) Python package, version
1.3.0 (L. M. Mentel, MIT licence), which collects them from these sources:

| Fields | Source |
|--------|--------|
| atomic weights | IUPAC CIAAW standard atomic weights (Meija et al., Pure Appl. Chem. 88, 265, 2016, and later CIAAW updates) |
| melting and boiling points, critical and triple points, allotropes | CRC Handbook of Chemistry and Physics, 97th ed. (W. M. Haynes, 2016) |
| density, electronegativity, van der Waals radius, abundances, molar heat capacity | CRC Handbook of Chemistry and Physics, 95th ed. (Haynes, 2014); densities of some heavy elements from Wikipedia |
| electron affinity | CRC Handbook (2014); T. Andersen et al., J. Phys. Chem. Ref. Data 33, 2004 |
| ionisation energies | NIST Atomic Spectra Database (Kramida, Ralchenko, Reader and NIST ASD Team) |
| atomic radius | J. C. Slater, J. Chem. Phys. 41, 3199 (1964) |
| covalent radii | B. Cordero et al., Dalton Trans. 2008, 2832; P. Pyykkö and M. Atsumi, Chem. Eur. J. 15, 186 (2009) |
| metallic radius | Kaye and Laby, Tables of Physical and Chemical Constants |
| oxidation states | Wikipedia, "List of oxidation states of the elements" (revision 1102394064) |
| isotopes, half-lives, natural abundances, discovery of isotopes | NUBASE2020 (F. G. Kondev et al., Chin. Phys. C 45, 030001, 2021) |
| Goldschmidt and geochemical classes | W. M. White, Geochemistry (2013) |
| CPK and Jmol colours | Wikipedia "CPK coloring"; Jmol |
| crystal structure, lattice constant, thermal conductivity, specific heat, heats of fusion and vaporisation, discovery, name origin, CAS numbers | mendeleev's element table (no citation given per value) |

The electron configuration is mendeleev's, with a lone electron written as
`4s1`. `phase_at_stp`, `melting_point_C`, `boiling_point_C`, the kJ/mol
values (1 eV = 96.485 kJ/mol), `electrons_per_shell`, `stable_isotopes`,
`most_stable_isotope` and `most_stable_half_life_s` are worked out by
`generate.py` from the values above.

## Rebuilding

The element folders are generated; edit `generate.py` or
`descriptions.txt`, not the `.md` files, and rebuild:

```bash
python -m venv venv
venv/Scripts/pip install mendeleev==1.3.0
venv/Scripts/python elements/generate.py
ussr tests/kgb_elements_test.su
```

`generate.py` deletes and rewrites `metals/`, `metalloids/` and
`nonmetals/`, so the result depends only on the mendeleev version and the
two files. Facts added by hand with `kgb add` or `kgb set` are lost on a
rebuild.
