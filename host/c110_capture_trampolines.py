"""Checked C1.10 displaced instructions for native capture entry wrappers.

No input file is modified. Only the explicitly enumerated first four words are
accepted; branch destinations inside the displaced block stay in that block.
No general instruction relocator, memory patch command or device I/O exists.
"""
import hashlib
import struct

ORIGINAL_SHA = '606d6116dc8e1560fb1608743a18840e0f7ab2617d11bcf02af025405aca1542'
BASE = 0x40160000
HOOKS = {'source': (1092989432, '847fe7ceeaa1585a5506f397b9bcec275c1251ea68a84c3dbf1d48f85b818484'), 'geometry': (1092990808, 'bab468ab05fd266f4aa86add9d7dbe317f9ff691e3301f8598a799893e28f286'), 'cpc': (1092996220, '0157cd0959a58d7375e4861fc7f1f17abb9e584561f3c5072b78ba813abc30d1'), 'raw_source': (1092996264, '10ab538796194147deca62c5028a43040e3b0c9e1d32bdbdf34d6e78ca6654da'), 'raw_stored': (1092996392, '10ab538796194147deca62c5028a43040e3b0c9e1d32bdbdf34d6e78ca6654da'), 'active': (1093266588, '2604701b2726c3c644e6025d89c67979c67c49e36893db46b3ea00cfd654abbd'), 'nrmt': (1093261124, '54e153085da562b485d72a25c2064f429be3bcb3fa3b8d7f4458d88e26f40ee7'), 'file': (1089669308, '2628f69b647f7ff8c0c2683bb0015d372237e237926c9890266e1ddd2684bcfb'), 'large': (1092999528, '4f278ff03ec747144bd02274cb5e643daddddbe7bfac699a9e63c06ae1e0a123'), 'small': (1093000328, '4f278ff03ec747144bd02274cb5e643daddddbe7bfac699a9e63c06ae1e0a123'), 'picture': (1092978716, '63c3a89c1fc827dfdd523421c1630f012c4a447f2938c572507f35327c131336'), 'picture_copy': (1094199908, '17d87f789f9727d8e0b95c41165fd783a22ddb29566d32144b2235b971ead646'), 'sensor': (1094103260, '706d75cc3764eb126196378ec3f197e1450313df9b1d6f5c8775ea8f5afaf10c'), 'pool': (1092896912, '8101ed47d58125f44a7b1a58b87867d113e7f62ff9f1a2f57d7edb05a5e4546f'), 'movie_metadata': (1081107360, 'a36127e716a9897cd4dd298fb5ff14151c64e1ec11d8184a07d89a811510f4de')}
RUNTIME_HOOKS = {'quick_template': (1079038464, '3c4004895c8a0160e837cb468b8798424a82727a23acaac95f6c3719d0c5b170'), 'quick_control': (1078883996, '05889355554c229a66708a5e8f7bb435b23028ae9ea0069363b70700d8ec6658'), 'quick_initialize': (1078950364, 'cffc00ce35bd91f55676f78ccb63c2b4498f0525c81c9cee165732a34b87c0d2'), 'quick_current': (1078967348, '636e009e1820957d4e5c0dbf0451ab45e1a686db4d186919f8ce0f5acb17b2be'), 'quick_commit': (1078954928, '3077bc17b6cd4cbebd5d1ec7f8e31ca302fd083651853b3955047bce2ced3182'), 'deferred': (1093478736, '5bf749dff631a73282098c7a2bcffd193358e609a3822a0b0527f32112ce22c4'), 'property': (1093477036, 'c6077940b3dd5e7ea5f97b9c603b88b5c2c81dd6a162e22b2a954c1803348192'), 'properties': (1093477896, '5bf749dff631a73282098c7a2bcffd193358e609a3822a0b0527f32112ce22c4'), 'resolution': (1079591988, '115294869fbba89538f28578d97c7e8a610b8e32ab666f9d99bd03f923814088'), 'lookup': (1077913608, '089dbe96ed4e21dfc61cf8c54a158d7d8171ee989ab2be3c70d00462ba9b0920'), 'initialize': (1091110812, '6d78d4ef18ea173b5333e19ca700f5e1268b4de0fa1152aa92322bcb2afb0b5a'), 'restart': (1091111136, '5bf749dff631a73282098c7a2bcffd193358e609a3822a0b0527f32112ce22c4'), 'build': (1091111460, '7172e2219d17a46dfd44a389e69953b2de2089ae61fef36ad01bee1e152833c6'), 'job': (1091116036, 'd74b75437296375d7cfbb91817cd5a3894985eb80a5c46776c810b68cb97fa9a')}


def emit(image, *, runtime=False):
    if hashlib.sha256(image).hexdigest() != ORIGINAL_SHA:
        raise ValueError('pinned original C1.10 required')
    out = ['.text']
    def absolute(target, label):
        out.extend([f'ldr x16,{label}', 'br x16', '.balign 8', f'{label}: .quad {target:#x}'])
    hooks = {**HOOKS, **(RUNTIME_HOOKS if runtime else {})}
    for name, (address, expected) in hooks.items():
        data = image[address-BASE:address-BASE+16]
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError('displaced instructions differ: ' + name)
        symbol = 'fx_c110_original_capture_' + name
        out.extend(['.balign 4', '.global ' + symbol, '.type ' + symbol + ',%function', symbol + ':'])
        for i, word in enumerate(struct.unpack('<4I', data)):
            label = f'.L_{name}_{i}'
            out.append(label + ':')
            conditional = word & 0xff000010 == 0x54000000
            compare = word & 0x7e000000 == 0x34000000
            if conditional or compare:
                displacement = (word >> 5) & 0x7ffff
                if displacement & 0x40000:
                    displacement -= 0x80000
                target = address + 4*i + displacement*4
                if conditional:
                    condition = ('eq','ne','hs','lo','mi','pl','vs','vc','hi','ls','ge','lt','gt','le','al','nv')[word & 15]
                    inverse = ('eq','ne','hs','lo','mi','pl','vs','vc','hi','ls','ge','lt','gt','le','al','nv')[(word & 15)^1]
                    branch, reverse = 'b.' + condition, 'b.' + inverse
                else:
                    register = ('x' if word >> 31 else 'w') + str(word & 31)
                    branch = ('cbnz' if word & (1 << 24) else 'cbz') + ' ' + register + ','
                    reverse = ('cbz' if word & (1 << 24) else 'cbnz') + ' ' + register + ','
                if address <= target < address + 16:
                    out.append(f'{branch} .L_{name}_{(target-address)//4}')
                else:
                    out.append(f'{reverse} {label}_fall')
                    absolute(target, label + '_literal')
                    out.append(label + '_fall:')
            else:
                # A changed profile cannot silently introduce another kind of
                # PC-relative instruction into an ordinary .inst copy.
                if ((word & 0x1f000000 == 0x10000000) or
                    (word & 0x3b000000 == 0x18000000) or
                    (word & 0x7c000000 == 0x14000000)):
                    raise ValueError('unhandled PC-relative displaced instruction')
                out.append(f'.inst {word:#010x}')
        absolute(address + 16, f'.L_{name}_continuation')
    return '\n'.join(out) + '\n'
