import os
import pptx
from pptx.util import Inches, Pt
from pptx.dml.color import RGBColor

pptx_path = r"d:\ULLASH personal\RVCE\SEMESTER 3\Classroom Subjects\DSA in C\PBL\GuardEscape_EL_Phase1.pptx"
prs = pptx.Presentation(pptx_path)

def set_run_style(run, font_name, font_size, bold, color_rgb):
    if font_name:
        run.font.name = font_name
    if font_size:
        run.font.size = font_size
    if bold is not None:
        run.font.bold = bold
    if color_rgb:
        run.font.color.rgb = color_rgb

def update_single_run_para(p, text, force_bold=None):
    font_name = "Century Gothic"
    font_size = None
    bold = force_bold
    color_rgb = None
    if p.runs:
        r0 = p.runs[0]
        font_name = r0.font.name or font_name
        font_size = r0.font.size
        if bold is None:
            bold = r0.font.bold
        try:
            if r0.font.color and r0.font.color.rgb:
                color_rgb = r0.font.color.rgb
        except Exception:
            pass
        r0.text = text
        for r in p.runs[1:]:
            r.text = ""
        set_run_style(r0, font_name, font_size, bold, color_rgb)
    else:
        p.text = text

def update_dual_run_para(p, prefix, text, prefix_bold=True, text_bold=False):
    font_name = "Century Gothic"
    font_size = None
    color_rgb = None
    if p.runs:
        r0 = p.runs[0]
        font_name = r0.font.name or font_name
        font_size = r0.font.size
        try:
            if r0.font.color and r0.font.color.rgb:
                color_rgb = r0.font.color.rgb
        except Exception:
            pass
        for r in p.runs:
            r.text = ""

    while len(p.runs) < 2:
        p.add_run()

    r1 = p.runs[0]
    r1.text = prefix
    set_run_style(r1, font_name, font_size, prefix_bold, color_rgb)

    r2 = p.runs[1]
    r2.text = text
    set_run_style(r2, font_name, font_size, text_bold, color_rgb)

def update_cell(cell, text):
    p = cell.text_frame.paragraphs[0]
    font_name = "Century Gothic"
    font_size = None
    bold = None
    color_rgb = None
    if p.runs:
        r0 = p.runs[0]
        font_name = r0.font.name or font_name
        font_size = r0.font.size
        bold = r0.font.bold
        try:
            if r0.font.color and r0.font.color.rgb:
                color_rgb = r0.font.color.rgb
        except Exception:
            pass
    cell.text = text
    p2 = cell.text_frame.paragraphs[0]
    if p2.runs:
        set_run_style(p2.runs[0], font_name, font_size, bold, color_rgb)

# 1. Update slide numbers cleanly across all 20 slides
for idx, slide in enumerate(prs.slides):
    slide_num_str = str(idx + 1)
    for shape in slide.shapes:
        if "Slide Number" in shape.name:
            if shape.has_text_frame and shape.text_frame.paragraphs:
                shape.text_frame.paragraphs[0].text = slide_num_str

# 2. SLIDE 3: Introduction
s3 = prs.slides[2]
# Shape 8 (TextBox 13) has 4 paragraphs
tb8 = s3.shapes[8].text_frame
update_single_run_para(tb8.paragraphs[0],
    "Guard Escape is an interactive 3D stealth thriller in C (raylib 5.5): a student locked in college corridors must collect golden keys, evade guards with tactical weapons and lockers, and reach the radiant violet exit portal.")
update_single_run_para(tb8.paragraphs[1],
    "Core gameplay executes on a 2D array grid (31 × 21); the 3D engine renders shaded corridors, dynamic lighting, interactive lockers, and retargeted Mixamo character rigs.")
update_single_run_para(tb8.paragraphs[2],
    "Every mechanic demonstrates a core DSA concept: Stack & DFS generate mazes, Circular Queue computes BFS distance fields, Binary Min-Heap powers real-time A* pathfinding, and a BST manages the leaderboard.")
update_single_run_para(tb8.paragraphs[3],
    "Interactive live pedagogy: [M] opens the live BFS Queue diagnostics dashboard and heat rings; [X] toggles A* Min-Heap vs Circular Queue BFS; [B] replays wave propagation live.")

# Shape 9 (TextBox 15)
tb9 = s3.shapes[9].text_frame
update_single_run_para(tb9.paragraphs[0],
    "Transforms abstract textbook data structures into a high-performance, real-time stealth game with direct runtime explainability.")

# 3. SLIDE 4: Problem Definition - Key Challenges
s4 = prs.slides[3]
tb4_8 = s4.shapes[8].text_frame
update_single_run_para(tb4_8.paragraphs[0],
    "DSA is taught through isolated console programs, so students rarely see how data structures work together, or how algorithm choice affects a real-time 60 FPS system.")

tb4_9 = s4.shapes[9].text_frame
update_single_run_para(tb4_9.paragraphs[0], "Key Challenges", force_bold=True)
update_dual_run_para(tb4_9.paragraphs[1], "Abstract Learning", " — stacks, queues, heaps and trees are usually only taught as static terminal programs")
update_dual_run_para(tb4_9.paragraphs[2], "Real-Time Constraint", " — pathfinding and collision across 651 tiles must execute within a 16.6ms frame budget")
update_dual_run_para(tb4_9.paragraphs[3], "Scaling AI Agents", " — naive per-guard pathfinding explodes as guards increase; needs a shared distance field")
update_dual_run_para(tb4_9.paragraphs[4], "Algorithm Comparison", " — textbooks contrast BFS and A* in theory; students need live side-by-side benchmarking")
update_dual_run_para(tb4_9.paragraphs[5], "Persistent Hierarchy", " — player scores and difficulties must persist across sessions with efficient ordered retrieval")
update_dual_run_para(tb4_9.paragraphs[6], "Visual Diagnostics", " — queue frontiers, explored node counts, and search paths must be visible for viva evaluation")

# 4. SLIDE 5: Problem Definition Table
s5 = prs.slides[4]
t5 = s5.shapes[8].table
table_s5_data = [
    ("Abstract learning", "5 data structures power mechanics: Stack, Circular Queue, Min-Heap, BST, Arrays"),
    ("Real-time pathfinding", "Dual engine: O(V+E) BFS Queue vs O((V+E) log V) A* Min-Heap [X toggle]"),
    ("Scaling enemies", "Single BFS from player yields shared distance field; guards step in O(1)"),
    ("Replayable levels", "Stack-based randomized DFS maze + 4 handcrafted architectural college maps"),
    ("Tactical stealth combat", "Stun darts (Z), Flash grenades (E), Coin lures (Q), Locker hiding (SPACE)"),
    ("Persistent ranking", "Dynamic BST leaderboard serialized to disk in pre-order; reloaded on boot"),
    ("Visual diagnostics", "In-game live AI BFS Queue data dashboard, distance rings & wave replay (M / B)")
]
for r_idx, (chal, app) in enumerate(table_s5_data, start=1):
    update_cell(t5.cell(r_idx, 0), chal)
    update_cell(t5.cell(r_idx, 1), app)

# 5. SLIDE 6: Objectives
s6 = prs.slides[5]
tb6 = s6.shapes[0].text_frame
obj_items = [
    "Build a playable 3D stealth thriller in C using raylib 5.5 and pure procedural audio synthesis",
    "Generate solvable mazes using a DFS backtracking stack, plus 4 handcrafted blueprints",
    "Implement a circular queue for optimal BFS graph traversal and the FIFO event message log",
    "Implement A* pathfinding powered by a custom Binary Min-Heap Priority Queue",
    "Drive guard AI with dual-mode navigation (BFS vs A*), auditory lures, and locker evasion",
    "Rank player scores with a dynamic Binary Search Tree (BST) and pre-order disk serialization",
    "Verify every data structure rigorously with 30 automated console unit tests"
]
for p_idx, text in enumerate(obj_items):
    update_single_run_para(tb6.paragraphs[p_idx], text)

# 6. SLIDE 7: Methodology - System Architecture Title
s7 = prs.slides[6]
tb7 = s7.shapes[0].text_frame
update_single_run_para(tb7.paragraphs[0], "System architecture: data-structure core (Stack, Queue, Min-Heap, BST), 3D renderer on top")

# 7. SLIDE 8: Data Structures: Maze Generation
s8 = prs.slides[7]
tb8 = s8.shapes[0].text_frame
update_single_run_para(tb8.paragraphs[0], "2D Array + Stack", force_bold=True)
update_single_run_para(tb8.paragraphs[1], "grid[21][31] stores walls, floor, lockers, pickups, and radiant violet exit portal; every gameplay rule reads this array")
update_single_run_para(tb8.paragraphs[2], "Randomised DFS: push the cell to stack, carve to an unvisited neighbour 2 cells away, pop at dead ends to backtrack")
update_single_run_para(tb8.paragraphs[3], "Braiding & Maps: removes 14 + 4×level extra walls to create loops and alternate routes; supports 4 handcrafted maps")

# 8. SLIDE 9: Data Structures: Circular Queue
s9 = prs.slides[8]
tb9 = s9.shapes[0].text_frame
update_single_run_para(tb9.paragraphs[0], "Circular Queue", force_bold=True)
update_single_run_para(tb9.paragraphs[1], "front / rear indices wrap with modulo arithmetic, so enqueue and dequeue are both guaranteed O(1)")
update_single_run_para(tb9.paragraphs[2], "Use 1: the BFS exploration frontier (circular FIFO ring buffer for shortest-path player distance field)")
update_single_run_para(tb9.paragraphs[3], "Use 2: on-screen tactical HUD log (5 slots with FIFO oldest-message eviction and 4s fadeout)")

# 9. SLIDE 10: Data Structures: BFS & Guard AI
s10 = prs.slides[9]
tb10 = s10.shapes[0].text_frame
update_single_run_para(tb10.paragraphs[0], "BFS on the Maze Graph", force_bold=True)
update_single_run_para(tb10.paragraphs[1], "Tiles = nodes, neighbouring floor tiles = edges; unweighted grid yields optimal shortest paths")
update_single_run_para(tb10.paragraphs[2], "PATROL: random corridor walk (60% forward persistence, turning at junctions and dead ends)")
update_single_run_para(tb10.paragraphs[3], "CHASE: engaged when player is within line-of-sight or 6 BFS steps; guards descend distance gradient")
update_single_run_para(tb10.paragraphs[4], "EVASION: guards lose trail after >11 steps or if player hides in a locker [SPACE]; stun darts [Z] freeze guards for 5s")

# 10. SLIDE 11: Data Structures: Key Design Idea
s11 = prs.slides[10]
tb11 = s11.shapes[0].text_frame
update_single_run_para(tb11.paragraphs[0], "One BFS for All Guards", force_bold=True)
update_single_run_para(tb11.paragraphs[1], "Naive: a separate BFS or A* from every guard costs O(guards × (V + E)) per frame")
update_single_run_para(tb11.paragraphs[2], "Ours: one BFS from the player gives every tile its shortest distance to her in a single pass")
update_single_run_para(tb11.paragraphs[3], "A guard just steps to an adjacent neighbour whose distance is one smaller, which is always an optimal step")
update_single_run_para(tb11.paragraphs[4], "So each extra chasing guard costs only O(1) per move, scaling effortlessly to 6+ guards")

# 11. SLIDE 12: Data Structures: Leaderboard
s12 = prs.slides[11]
tb12 = s12.shapes[0].text_frame
update_single_run_para(tb12.paragraphs[0], "BST + File Handling", force_bold=True)
update_single_run_para(tb12.paragraphs[1], "Dynamic Binary Search Tree keyed by player score; insertion is O(h) (average O(log n))")
update_single_run_para(tb12.paragraphs[2], "Reverse in-order traversal (Right -> Node -> Left) yields the Top-10 leaderboard in strictly descending order")
update_single_run_para(tb12.paragraphs[3], "Rank determination: computed by counting nodes with greater score during tree traversal")
update_single_run_para(tb12.paragraphs[4], "Saved to leaderboard.txt in pre-order (N -> L -> R), so file reloading reconstructs the exact identical tree topology")

# 12. SLIDE 13: Data Collection: Textures
s13 = prs.slides[12]
tb13_12 = s13.shapes[12].text_frame
update_single_run_para(tb13_12.paragraphs[0], "How They Are Used", force_bold=True)
update_single_run_para(tb13_12.paragraphs[1], "Each surface uses an albedo (colour) map + a normal map")
update_single_run_para(tb13_12.paragraphs[2], "Lit by the lantern, guard torches, flickering tube lights and radiant violet exit portal")

# 13. SLIDE 16: Complexity Analysis Table
s16 = prs.slides[15]
t16 = s16.shapes[8].table
table_s16_data = [
    ("Maze generation (randomised DFS with stack)", "O(R × C) = O(V + E)"),
    ("BFS from the player (runs every step)", "O(V + E) optimal, with V = 651"),
    ("A* pathfinding (Binary Min-Heap Priority Queue)", "O((V + E) log V) with heuristic pruning"),
    ("Guard move decision (look up 4 neighbours)", "O(1)"),
    ("Enqueue / Dequeue (circular queue)", "O(1)"),
    ("Binary Min-Heap push & extract-min", "O(log N)"),
    ("BST insert (leaderboard) & ranking", "O(h) avg O(log n) / O(n) reverse in-order")
]
for r_idx, (op, comp) in enumerate(table_s16_data, start=1):
    update_cell(t16.cell(r_idx, 0), op)
    update_cell(t16.cell(r_idx, 1), comp)

# 14. SLIDE 17: Literature Review Table
s17 = prs.slides[16]
t17 = s17.shapes[0].table
# Row 2 is Hart, Nilsson & Raphael, 1968 (A* heuristic search)
update_cell(t17.cell(2, 0), "Hart, Nilsson & Raphael, 1968")
update_cell(t17.cell(2, 1), "A* heuristic search with priority queue")
update_cell(t17.cell(2, 2), "Directs search toward target via f(n) = g(n) + h(n)")
update_cell(t17.cell(2, 3), "Implemented via Binary Min-Heap; switchable with [X] key for live runtime comparison against BFS")

# 15. SLIDE 18: Testing & Future Scope
s18 = prs.slides[17]
tb18 = s18.shapes[0].text_frame
update_dual_run_para(tb18.paragraphs[0], "Testing: ", "30 automated unit tests (tests/test_ds.c) validating Stack, Circular Queue, BFS, 4 Maps, BST, and A* Min-Heap; run via build.bat test")
update_dual_run_para(tb18.paragraphs[1], "Challenge: ", "Mixamo student had no walk cycle, solved with bone retargeting; camera collision solved with spring-arm & wall cut-aways")
update_dual_run_para(tb18.paragraphs[2], "Stealth Combat: ", "Integrated single-target stun darts [Z], radial flash grenades [E], coin lures [Q], and locker hiding sanctuary [SPACE]")
update_dual_run_para(tb18.paragraphs[3], "Status: ", "All 5 data structures, procedural audio synthesizer, dual AI pathfinders, HUD diagnostics [M], and 4 handcrafted maps are 100% playable")
update_dual_run_para(tb18.paragraphs[4], "Future: ", "Self-balancing AVL tree for guaranteed O(log N) leaderboard, weighted terrain graphs with Dijkstra search, and multi-story floors")

prs.save(pptx_path)
print("Successfully updated GuardEscape_EL_Phase1.pptx with all latest features!")
