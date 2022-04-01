# 3/5/2022

I started the project today. I am working on a renderer. It is built with
Vulkan, which I have used to write simple renderers in the past.  I referenced
the spec along with source code to a simple Vulkan triangle example, and I got
the triangle rendering. [Here is the image](first_triangle.png).

One of the biggest benefits of Vulkan to me is the ability to "ahead of time"
compile VkPipeline objects.  Other APIs create them JIT and intern them to
avoid too much frame pausing.  With Vulkan, if I know what I am going to be
rendering at compile time (I do 99% of the time), I can break off a chunk of a
thread pool and compile them all at load time.

To do this, I have implemented a "technique" system similar to Direct3D Effects.
In a JSON file all the render pipelines can be specified, and then this config
file is loaded at load time and all the pipelines will be compiled.  This also
opens up the opportunity for easy pipeline hot reloading.  I implemented a
simple loader with cJSON, but I don't like the library much.  More work will
be done.

I think that I will work on setting up a model loader tomorrow, I will be using
the glTF 2.0 format.

# 3/6/2022

I have begun work on the glTF format loader.  I changed my library from a real
JSON parser (cJSON) to jsmn, which is just a JSON tokenizer.  I will be handling
the stack information myself, most of it implicit in my recursive descent
parser's recursions.

Cross referencing the glTF spec has been a great help.

I finished the glTF loader by the end of the day.  It supports just a tiny
subsection of the spec, but its enough to export a cube from Maya and load it in
as vertex, normal, and index data.  I need to write a mesh loader.

# 3/7/2022

I got meshes loading and rendering, although only from the side, and in a poor
orthographic projection, as I have yet to implement uniforms to get cameras
working.  I have a strange bug where curved objects do not render in their 
entirety, and I am not sure why, this is visible [here](curved-cuttoff.png).

I used a very simple method of just uploading some data to Vulkan buffers, and
rendering them with vkCmdDrawIndexed.  It is not particularly efficient, and
it can only render 1 mesh at a time.  In the end, I want a comfortable API where
I can list off objects to render with a simple draw command at the end of each
simulation frame, and then the renderer will sort them and cull them as needed.

I also have a problem with shaders. I am currently using GLSL compiled to
SPIR-V with the open source Khronos compiler.  The compiler works great
(though its a pain to have to recompile when I change the shader), but it has no
reflection capabilities!  I will either have to write my own shader language,
write a parser for GLSL, or write a parser for SPIR-V (is this possible?) to get
reflection data.

Either way, a productive day and a step further.

# 3/8/2022

I have decided to write my own shading language. It will be easy to extract
reflection data from, easier to control my optimizer, and easier to implement
hot reloaded (I do not like the GLSL C libraries available).

I'm calling it BSL, for Bean's Shader Language.  It will be styled after
Standard ML.  This will be easier to implement than most other languages I have
implemented, because I do not have some of the design requirements that those
languages have. First, it doesn't need to do heavy optimization, SPIR-V is
already heavily optimized by the GPU driver.  Second, I don't need any error
reporting beyond a message printed and quitting.  Third, it is a much smaller
language with less surface area.

I implemented the basic layout of a lexer, but never got around to lexing much.
Some friends were leaving town for a long time and I went to say goodbye.
Work will continue tomorrow, hopefully with a functioning lexer by the end of
the day.

# 3/9/2022

Continuing work on the lexer, and I have keywords, symbols, and punctuation
implemented the format is really simple, just switching over the characters and
calling more specific functions to lex subcases.  It isn't super efficient,
becasue its full of branch mispredictions and such, but lexing isn't the
bottleneck (and neither is compiling shaders, really).  I have the tokens
carrying location info just to the granularity of line and column, which is
enough for simple errors.

Finished lexing numbers and such, and getting proper error reporting.  I have
realized an important design constraint I should have seen before.  Very low
allocation!  I will have to allocate a chunk of memory to store the SPIR-V,
certainly but currently the lexer is allocation free, and if I can agree on maximum
sizes for AST nodes and such I could preallocate all of them before parsing,
saving a lot that precious memory from fragmentation. Performance matters!

Anyway, I will continue to work on this, probably looking into parsing and
codegen tomorrow, I will have to see how much of an AST I really need, and
how much I can just do in a single pass.

# 3/10/2022

Finished the lexer, working on parsing and codegen.  I got a very simple
template working, printing the magic number, maximum result, etc.  Codegen is
going to be difficult due to the SSA format SPIR-V uses, but it should be
workable.

# 3/11/2022

I have gotten types compiling, and added decorations for builtins like position.
Procedures are still a mess.  I am using a big mess of single pass compilation,
while generating SPIR-V for the types, decorations, prototypes, etc. at the end.
I will have the procedures generate SPIR-V while I parse however.  This is still
unimplemented, but will be what I am working on going for.  As for now, we have
vectors, structs, function names, and I/O globals,

# 3/12/2022
# 3/13/2022

No progress was made these days because I was stuck in the airport waiting for
a flight that never came.

# 3/14/2022

Getting frustrated with SDL2, so I am writing a simple windowing library.
I implemented all the features I needed for the renderer as is, so I am getting
back to shader compilation.  The repo can be found
[here](https://github.com/garfr/cwin).

It currently only supports Win32, and is missing some of features I will
eventually need (fullscreen, keyboard input, etc), but it is much more
convenient to work with than SDL2.

I will swap out SDL2 for cwin tomorrow, which will require writing a timing
subsystem and creating a Vulkan surface on my own, as SDL2 was doing it for me
before.

# 3/15/2022

I implmeented fullscreening and maximum/minimum window sizes this morning in
CWin, but I still haven't ported Miur to the library yet, I got distracted with
the compiler.  Today was super productive for the compiler, I implemented
variables (local, and global), returning, assignment, constants, so you can
compile very simple programs.  The biggest missing feature is vectors values,
so that you can output vertex positions, colors, etc.  Its really getting there
however, and I hope to have a triangle rendered with it by tomorrow.

The complexity of the whole ordeal continues to bubble though, as I ideally
compile everything in one pass, but the rigid structure of SPIR-V requires
me to record some things (types, procedure names, global inputs) to be spit
out all together.  So even though in BSL you can define global inputs wherever,
you can only declare them at the beginning of a file together.

So far working well, although only real debugging can be trusted.  It is funny
though, the struct that stores the parsers state is 20kb alone, because of all
the values preallocated inside of it.

# 3/16/2022

Today I am implementing vector literals and vector operations.  It took very
little work suprisingly to get vector construction working.  I record the size of
each expression in the block, and then ensure they are under 4, then its as
simple as outputing a SpvOpCompositeConstruct operation, which can take vector
parameters.  I implemented this, fixed a bug where variables labeled with
[builtin(position)] weren't being decorated with the output decoration, and
then plugged the compiler into the renderer.  Debugged for a little bit and
we have a [triangle](bsl-white-triangle.png)!.  Very exciting.  I was also
able to output the normals with a vector assignment as shown
[here](bsl-normal-triangle.png).  It is satisfying to finally be drawing
something with my language.  It's still not useful for much besides writing
inputs to outputs, so I will have to implement math operations and matrixes.

# 3/31/2022

I feel silly, I have left this log untouched for several weeks now.  I got off
my spring break, so I guess that I have just been more busy with school and
social interactions.  Anyway, progress has still been steady.  I implemented
the beginnings of a material system, with effects, techniques and materials.
I still need support for textures and descriptors, so you can't pass uniforms
or anything as is.  I also implemented a render graph that calculates an
efficient rendering procedure to draw different "passes", each with specified
inputs and ouputs.  Finally, I implemented shader hot reloading on top of the
material system, so its easy to play with hard coded values to get the look you
want. 

I will begin working on passing uniform data to shaders tomorrow, so maybe
I can do some model/view/projection action.
