#!/usr/bin/env python3
"""Generates the Vulkan thunks of vulkanRecording.cpp from this checkout's registry and dispatcher.

Every populated slot of Vulkan-Hpp's dynamic dispatcher gets a thunk. A command-buffer command
(vkCmd*) whose arguments can be copied (values, flat arrays, and the structures the writer
copies deeply) is queued for the recording thread when the calling thread records; so are the
few host calls that must keep their order relative to the recorded commands. Every other call
first waits until the recording thread has replayed what the calling thread recorded, unless
the call cannot depend on those commands, and then goes to the driver as before.

The idea and the shape of this generator follow chenxiao07's KytyPS5 fork
(src/local/generate-vulkan-recording.py, branch experiment/perf-40fps-20260926).

usage: generateVulkanRecording.py <vk.xml> <vulkan.hpp> <output.inc>
"""
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

# Structures with pointers that vulkanRecording.cpp's Writer copies deeply.
DEEP_COPIES = {'VkWriteDescriptorSet', 'VkDependencyInfo', 'VkRenderingInfo', 'VkCommandBufferBeginInfo'}

# Host calls queued in order with the recorded commands instead of waiting for them: descriptor
# writes and view destruction name objects the commands before and after them use.
ORDERED_HOST_CALLS = {'vkUpdateDescriptorSets', 'vkDestroyImageView'}

# Calls with a result that are queued as well: the begin and the end of a command buffer the
# recording thread fills. The thunk answers VK_SUCCESS; the recording thread checks the driver's
# answer.
ORDERED_RESULT_CALLS = {'vkBeginCommandBuffer', 'vkEndCommandBuffer'}

# Calls that neither depend on commands still to be replayed nor touch what the recording thread
# uses, so they need no wait: progress queries and waits (every submission is handed to the
# recording thread at once, so a tick that is waited for is on its way), new objects that no
# queued command can name, descriptor sets taken from a pool (the recording thread writes sets,
# never the pool), and names for objects.
NO_WAIT_CALLS = {
    'vkWaitSemaphores', 'vkWaitSemaphoresKHR', 'vkAllocateDescriptorSets', 'vkAllocateMemory',
    'vkBindBufferMemory', 'vkBindImageMemory', 'vkBindBufferMemory2', 'vkBindImageMemory2',
    'vkMapMemory', 'vkUnmapMemory', 'vkFlushMappedMemoryRanges', 'vkInvalidateMappedMemoryRanges',
    'vkSetDebugUtilsObjectNameEXT', 'vkSetDebugUtilsObjectTagEXT',
}
# ... except the ones that use a command pool, which the recording thread's commands use too.
POOL_CALLS = {'vkCreateCommandPool'}


def generate(registry, header):
    root = ET.parse(registry).getroot()
    types = {e.get('name') or e.findtext('name'): e for e in root.findall('./types/type')}
    commands = {e.get('name') or e.findtext('proto/name'): e for e in root.findall('./commands/command')}

    def vulkan(element):
        return 'vulkan' in element.get('api', 'vulkan').split(',')

    def decl(element):
        # Registry comments are prose, not part of a C prototype.
        return ''.join(element.itertext()).split('//')[0].strip()

    def flat(name, seen=()):
        if name in seen:
            return False
        t = types.get(name)
        if t is None:
            return name != 'void'
        if t.get('alias'):
            return flat(t.get('alias'), (*seen, name))
        if t.get('category') not in ('struct', 'union'):
            return True
        return all('*' not in decl(m) and flat(m.findtext('type'), (*seen, name))
                   for m in t.findall('member') if vulkan(m))

    def copyable(name):
        t = types.get(name)
        if name in DEEP_COPIES or flat(name):
            return True
        if t is None:
            return False
        if t.get('alias'):
            return copyable(t.get('alias'))
        members = [m for m in t.findall('member') if vulkan(m)]
        # A structure whose only pointer is pNext: copied when the chain is empty (the writer
        # checks), sent to the driver at once otherwise.
        return (t.get('category') == 'struct' and
                any(m.findtext('name') == 'pNext' for m in members) and
                all(m.findtext('name') == 'pNext' or
                    ('*' not in decl(m) and flat(m.findtext('type'))) for m in members))

    def no_wait(name):
        if name in POOL_CALLS:
            return False
        return name in NO_WAIT_CALLS or name.startswith('vkCreate') or name.startswith('vkGet')

    declarations, installs, names, queued = [], [], [], []
    section = header.read_text(encoding='utf-8').split('class DispatchLoaderDynamic :', 1)[1].split(
        'DispatchLoaderDynamic()', 1)[0]
    for line in section.splitlines():
        if line.lstrip().startswith('#'):
            # The platform guards of the dispatcher's own declarations.
            declarations.append(line.strip())
            installs.append(line.strip())
            continue
        match = re.match(r'\s*PFN_(vk\w+)\s+\1\s*=\s*0;', line)
        if not match:
            continue
        name = match[1]
        element = commands[name]
        while element.get('alias'):
            element = commands[element.get('alias')]
        params = [p for p in element.findall('param') if vulkan(p)]
        pnames = [p.findtext('name') for p in params]
        ret = element.findtext('proto/type')
        copies, args = [], []
        ok = ((name.startswith('vkCmd') or name in ORDERED_HOST_CALLS) and ret == 'void') or \
            (name in ORDERED_RESULT_CALLS and ret == 'VkResult')
        # A host allocator cannot be copied; the renderer never passes one. Such a call is queued
        # only without it.
        allocator = False
        for p in params:
            n, t, d = p.findtext('name'), p.findtext('type'), decl(p)
            if n == 'pAllocator' and t == 'VkAllocationCallbacks':
                allocator = True
                args.append('nullptr')
                continue
            if '*' not in d and '[' not in d:
                args.append(n)
                continue
            size = p.get('len')
            array = re.search(r'\[(\d+)\]', d)
            if array:
                size = array[1]
            # No pointer to a pointer, output pointer, string or computed length.
            if '**' in d or not d.startswith('const ') or (not copyable(t) and t != 'void'):
                ok = False
            if size is None:
                size = '1'
            if not (size.isdigit() or size in pnames):
                ok = False
            if t == 'void' and size == '1':
                ok = False
            copies.append(f'auto copied_{n} = writer.Copy({n}, {size});')
            args.append('copied_' + n)
        index = len(names)
        names.append(name)
        declarations.append(
            f'static VKAPI_ATTR {ret} VKAPI_CALL Wrapped_{name}({", ".join(decl(p) for p in params)}) {{')
        if ok:
            queued.append(name)
            call = f'original.{name}({", ".join(args)})'
            if ret == 'VkResult':
                call = f'CheckReplayed({call}, {index})'
            declarations.append('    if (auto* stream = pAllocator == nullptr ? RecordingStream() : nullptr) {'
                                if allocator else '    if (auto* stream = RecordingStream()) {')
            declarations.append('        if (stream->Enqueue([&](Writer& writer) {')
            declarations.extend('            ' + c for c in copies)
            declarations.append(f'            writer.Command([=] {{ {call}; }});')
            declarations.append('        })) { ' + ('return VK_SUCCESS;' if ret == 'VkResult' else 'return;') + ' }')
            declarations.append('    }')
        if not no_wait(name):
            declarations.append(f'    BeforeDirect({index});')
        declarations.append(f'    return original.{name}({", ".join(pnames)});')
        declarations.append('}')
        installs.append(f'if (original.{name}) dispatcher.{name} = Wrapped_{name};')
    table = 'static const char* const g_thunk_names[] = {\n' + ''.join(f'    "{n}",\n' for n in names) + '};\n'
    return ('// Generated by generateVulkanRecording.py; edit that file, not this one.\n' + table +
            '\n'.join(declarations) + '\nstatic void InstallThunks() {\n' +
            'auto& dispatcher = VULKAN_HPP_DEFAULT_DISPATCHER;\noriginal = dispatcher;\n' +
            '\n'.join(installs) + '\n}\n'), names, queued


if __name__ == '__main__':
    result, names, queued = generate(Path(sys.argv[1]), Path(sys.argv[2]))
    Path(sys.argv[3]).write_text(result, encoding='utf-8')
    print(f'Vulkan recording: {len(queued)} queued commands, {len(names)} dispatcher entries')
