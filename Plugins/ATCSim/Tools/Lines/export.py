# Reads what Tools/Lines needs out of the open EHAM map into Saved/ATC/Lines: taxi routes, stands, yellow and white paint, pavement.
import os
import struct

import unreal

OUT = os.environ.get('ATC_LINES_DIR') or os.path.join(unreal.Paths.project_saved_dir(), 'ATC', 'Lines')
os.makedirs(OUT, exist_ok=True)
WORLD = unreal.SplineCoordinateSpace.WORLD
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()

# Routes: the spline points, a sample every metre, the stops
with open(os.path.join(OUT, 'routes.txt'), 'w') as f, open(os.path.join(OUT, 'stands.txt'), 'w') as st:
    for a in actors:
        if isinstance(a, unreal.TaxiRoute):
            sp = a.get_editor_property('spline')
            length = sp.get_spline_length()
            tag = str(a.get_editor_property('route_tag').get_editor_property('tag_name'))
            f.write('R %s %.1f %d %s\n' % (tag, length, sp.get_number_of_spline_points(), a.get_actor_label()))
            for i in range(sp.get_number_of_spline_points()):
                p = sp.get_location_at_spline_point(i, WORLD)
                f.write('C %.1f %.1f %.1f\n' % (p.x, p.y, p.z))
            d = 0.0
            while d < length + 99.0:
                p = sp.get_location_at_distance_along_spline(min(d, length), WORLD)
                f.write('%.1f %.1f %.1f\n' % (p.x, p.y, p.z))
                d += 100.0
            for s in a.get_editor_property('stops'):
                f.write('S %s %.1f\n' % (str(s.get_editor_property('name')).replace(' ', '_'), s.get_editor_property('distance')))
        elif isinstance(a, unreal.Stand):
            t = a.get_parking_transform()
            st.write('%s %.1f %.1f %.2f\n' % (a.get_editor_property('stand_tag').get_editor_property('tag_name'), t.translation.x, t.translation.y, t.rotation.rotator().yaw))

# Paint and pavement: the triangles of the decal meshes by material
KINDS = [('Markings_Yellow', 'paint.bin', 0), ('RunwayMarkings', 'white.bin', 0), ('Decal_AsphaltLight', 'pave.bin', 1), ('Decal_AsphaltMedium', 'pave.bin', 2),
         ('Decal_AsphaltDark', 'pave.bin', 3), ('Decal_ApronLight', 'pave.bin', 4), ('Decal_RWY_Dark', 'pave.bin', 5)]
files = {name: open(os.path.join(OUT, name), 'wb') for name in ('paint.bin', 'white.bin', 'pave.bin')}
for a in actors:
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        m = c.get_editor_property('static_mesh')
        if not m or not c.is_visible():
            continue
        for i in range(c.get_num_materials()):
            mat = c.get_material(i)
            kind = next((k for k in KINDS if mat and k[0] in mat.get_name()), None)
            if not kind:
                continue
            v, t, n, uv, tan = unreal.ProceduralMeshLibrary.get_section_from_static_mesh(m, 0, i)
            tf = c.get_world_transform()
            p = [tf.transform_location(q) for q in v]
            t = list(t)
            number = kind[2] if kind[2] else (1 if a.get_actor_label().startswith('HoldMarkings') else 0)
            for k in range(0, len(t) - 2, 3):
                q = (p[t[k]], p[t[k + 1]], p[t[k + 2]])
                files[kind[1]].write(struct.pack('<6fi', q[0].x, q[0].y, q[1].x, q[1].y, q[2].x, q[2].y, number))
for f in files.values():
    f.close()
mask = os.path.join(OUT, 'pave_mask.bin')   # made again from pave.bin
if os.path.exists(mask):
    os.remove(mask)
unreal.log('Tools/Lines: map read into %s' % OUT)
