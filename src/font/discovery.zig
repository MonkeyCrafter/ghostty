const std = @import("std");
const Allocator = std.mem.Allocator;
const assert = @import("../quirks.zig").inlineAssert;
const fontconfig = @import("fontconfig");
const macos = @import("macos");
const opentype = @import("opentype.zig");
const options = @import("main.zig").options;
const Collection = @import("main.zig").Collection;
const DeferredFace = @import("main.zig").DeferredFace;
const Variation = @import("main.zig").face.Variation;

const log = std.log.scoped(.discovery);

/// Discover implementation for the compile options.
pub const Discover = switch (options.backend) {
    .freetype => void, // no discovery
    .fontconfig_freetype => Fontconfig,
    .web_canvas => void, // no discovery
    .coretext,
    .coretext_freetype,
    .coretext_harfbuzz,
    .coretext_noshape,
    => CoreText,
    .directwrite_freetype => DirectWrite,
};

/// Descriptor is used to search for fonts. The only required field
/// is "family". The rest are ignored unless they're set to a non-zero
/// value.
pub const Descriptor = struct {
    /// Font family to search for. This can be a fully qualified font
    /// name such as "Fira Code", "monospace", "serif", etc. Memory is
    /// owned by the caller and should be freed when this descriptor
    /// is no longer in use. The discovery structs will never store the
    /// descriptor.
    ///
    /// On systems that use fontconfig (Linux), this can be a full
    /// fontconfig pattern, such as "Fira Code-14:bold".
    family: ?[:0]const u8 = null,

    /// Specific font style to search for. This will filter the style
    /// string the font advertises. The "bold/italic" booleans later in this
    /// struct filter by the style trait the font has, not the string, so
    /// these can be used in conjunction or not.
    style: ?[:0]const u8 = null,

    /// A codepoint that this font must be able to render.
    codepoint: u32 = 0,

    /// Font size in points that the font should support. For conversion
    /// to pixels, we will use 72 DPI for Mac and 96 DPI for everything else.
    /// (If pixel conversion is necessary, i.e. emoji fonts)
    size: f32 = 0,

    /// True if we want to search specifically for a font that supports
    /// specific styles.
    bold: bool = false,
    italic: bool = false,
    monospace: bool = false,

    /// Variation axes to apply to the font. This also impacts searching
    /// for fonts since fonts with the ability to set these variations
    /// will be preferred, but not guaranteed.
    variations: []const Variation = &.{},

    /// Hash the descriptor with the given hasher.
    pub fn hash(self: Descriptor, hasher: anytype) void {
        const autoHash = std.hash.autoHash;
        const autoHashStrat = std.hash.autoHashStrat;
        autoHashStrat(hasher, self.family, .Deep);
        autoHashStrat(hasher, self.style, .Deep);
        autoHash(hasher, self.codepoint);
        autoHash(hasher, @as(u32, @bitCast(self.size)));
        autoHash(hasher, self.bold);
        autoHash(hasher, self.italic);
        autoHash(hasher, self.monospace);
        autoHash(hasher, self.variations.len);
        for (self.variations) |variation| {
            autoHash(hasher, variation.id);

            // This is not correct, but we don't currently depend on the
            // hash value being different based on decimal values of variations.
            autoHash(hasher, @as(i64, @intFromFloat(variation.value)));
        }
    }

    /// Returns a hash code that can be used to uniquely identify this
    /// action.
    pub fn hashcode(self: Descriptor) u64 {
        var hasher = std.hash.Wyhash.init(0);
        self.hash(&hasher);
        return hasher.final();
    }

    /// Deep copy of the struct. The given allocator is expected to
    /// be an arena allocator of some sort since the descriptor
    /// itself doesn't support fine-grained deallocation of fields.
    pub fn clone(self: *const Descriptor, alloc: Allocator) !Descriptor {
        // We can't do any errdefer cleanup in here. As documented we
        // expect the allocator to be an arena so any errors should be
        // cleaned up somewhere else.

        var copy = self.*;
        copy.family = if (self.family) |src| try alloc.dupeZ(u8, src) else null;
        copy.style = if (self.style) |src| try alloc.dupeZ(u8, src) else null;
        copy.variations = try alloc.dupe(Variation, self.variations);
        return copy;
    }

    /// Convert to Fontconfig pattern to use for lookup. The pattern does
    /// not have defaults filled/substituted (Fontconfig thing) so callers
    /// must still do this.
    pub fn toFcPattern(self: Descriptor) *fontconfig.Pattern {
        const pat = fontconfig.Pattern.create();
        if (self.family) |family| {
            assert(pat.add(.family, .{ .string = family }, false));
        }
        if (self.style) |style| {
            assert(pat.add(.style, .{ .string = style }, false));
        }
        if (self.codepoint > 0) {
            const cs = fontconfig.CharSet.create();
            defer cs.destroy();
            assert(cs.addChar(self.codepoint));
            assert(pat.add(.charset, .{ .char_set = cs }, false));
        }
        if (self.size > 0) assert(pat.add(
            .size,
            .{ .integer = @intFromFloat(@round(self.size)) },
            false,
        ));
        if (self.bold) assert(pat.add(
            .weight,
            .{ .integer = @intFromEnum(fontconfig.Weight.bold) },
            false,
        ));
        if (self.italic) assert(pat.add(
            .slant,
            .{ .integer = @intFromEnum(fontconfig.Slant.italic) },
            false,
        ));

        // For fontconfig, we always add monospace in the pattern. Since
        // fontconfig sorts by closeness to the pattern, this doesn't fully
        // exclude non-monospace but helps prefer it.
        assert(pat.add(
            .spacing,
            .{ .integer = @intFromEnum(fontconfig.Spacing.mono) },
            false,
        ));

        return pat;
    }

    /// Convert to Core Text font descriptor to use for lookup or
    /// conversion to a specific font.
    pub fn toCoreTextDescriptor(self: Descriptor) !*macos.text.FontDescriptor {
        const attrs = try macos.foundation.MutableDictionary.create(0);
        defer attrs.release();

        // Family
        if (self.family) |family_bytes| {
            const family = try macos.foundation.String.createWithBytes(family_bytes, .utf8, false);
            defer family.release();
            attrs.setValue(
                macos.text.FontAttribute.family_name.key(),
                family,
            );
        }

        // Style
        if (self.style) |style_bytes| {
            const style = try macos.foundation.String.createWithBytes(style_bytes, .utf8, false);
            defer style.release();
            attrs.setValue(
                macos.text.FontAttribute.style_name.key(),
                style,
            );
        }

        // Codepoint support
        if (self.codepoint > 0) {
            const cs = try macos.foundation.CharacterSet.createWithCharactersInRange(.{
                .location = self.codepoint,
                .length = 1,
            });
            defer cs.release();
            attrs.setValue(
                macos.text.FontAttribute.character_set.key(),
                cs,
            );
        }

        // Set our size attribute if set
        if (self.size > 0) {
            const size32: i32 = @intFromFloat(@round(self.size));
            const size = try macos.foundation.Number.create(
                .sint32,
                &size32,
            );
            defer size.release();
            attrs.setValue(
                macos.text.FontAttribute.size.key(),
                size,
            );
        }

        // Build our traits. If we set any, then we store it in the attributes
        // otherwise we do nothing. We determine this by setting up the packed
        // struct, converting to an int, and checking if it is non-zero.
        const traits: macos.text.FontSymbolicTraits = .{
            .bold = self.bold,
            .italic = self.italic,
            .monospace = self.monospace,
        };
        const traits_cval: u32 = @bitCast(traits);
        if (traits_cval > 0) {
            // Setting traits is a pain. We have to create a nested dictionary
            // of the symbolic traits value, and set that in our attributes.
            const traits_num = try macos.foundation.Number.create(
                .sint32,
                @as(*const i32, @ptrCast(&traits_cval)),
            );
            defer traits_num.release();

            const traits_dict = try macos.foundation.MutableDictionary.create(0);
            defer traits_dict.release();
            traits_dict.setValue(
                macos.text.FontTraitKey.symbolic.key(),
                traits_num,
            );

            attrs.setValue(
                macos.text.FontAttribute.traits.key(),
                traits_dict,
            );
        }

        return try macos.text.FontDescriptor.createWithAttributes(@ptrCast(attrs));
    }
};

pub const Fontconfig = struct {
    fc_config: *fontconfig.Config,

    pub fn init() Fontconfig {
        // safe to call multiple times and concurrently
        _ = fontconfig.init();
        return .{ .fc_config = fontconfig.initLoadConfigAndFonts() };
    }

    pub fn deinit(self: *Fontconfig) void {
        self.fc_config.destroy();
    }

    /// Discover fonts from a descriptor. This returns an iterator that can
    /// be used to build up the deferred fonts.
    pub fn discover(
        self: *const Fontconfig,
        alloc: Allocator,
        desc: Descriptor,
    ) !DiscoverIterator {
        _ = alloc;

        // Build our pattern that we'll search for
        const pat = desc.toFcPattern();
        errdefer pat.destroy();
        assert(self.fc_config.substituteWithPat(pat, .pattern));
        pat.defaultSubstitute();

        // Search
        const res = self.fc_config.fontSort(pat, false, null);
        if (res.result != .match) return error.FontConfigFailed;
        errdefer res.fs.destroy();

        return .{
            .config = self.fc_config,
            .pattern = pat,
            .set = res.fs,
            .fonts = res.fs.fonts(),
            .variations = desc.variations,
            .i = 0,
        };
    }

    pub fn discoverFallback(
        self: *const Fontconfig,
        alloc: Allocator,
        collection: *Collection,
        desc: Descriptor,
    ) !DiscoverIterator {
        _ = collection;
        return try self.discover(alloc, desc);
    }

    pub const DiscoverIterator = struct {
        config: *fontconfig.Config,
        pattern: *fontconfig.Pattern,
        set: *fontconfig.FontSet,
        fonts: []*fontconfig.Pattern,
        variations: []const Variation,
        i: usize,

        pub fn deinit(self: *DiscoverIterator) void {
            self.set.destroy();
            self.pattern.destroy();
            self.* = undefined;
        }

        pub fn next(self: *DiscoverIterator) fontconfig.Error!?DeferredFace {
            if (self.i >= self.fonts.len) return null;

            // Get the copied pattern from our fontset that has the
            // attributes configured for rendering.
            const font_pattern = try self.config.fontRenderPrepare(
                self.pattern,
                self.fonts[self.i],
            );
            errdefer font_pattern.destroy();

            // Increment after we return
            defer self.i += 1;

            return DeferredFace{
                .fc = .{
                    .pattern = font_pattern,
                    .charset = (try font_pattern.get(.charset, 0)).char_set,
                    .langset = (try font_pattern.get(.lang, 0)).lang_set,
                    .variations = self.variations,
                },
            };
        }
    };
};

pub const CoreText = struct {
    pub fn init() CoreText {
        // Required for the "interface" but does nothing for CoreText.
        return .{};
    }

    pub fn deinit(self: *CoreText) void {
        _ = self;
    }

    /// Discover fonts from a descriptor. This returns an iterator that can
    /// be used to build up the deferred fonts.
    pub fn discover(self: *const CoreText, alloc: Allocator, desc: Descriptor) !DiscoverIterator {
        _ = self;

        // Build our pattern that we'll search for
        const ct_desc = try desc.toCoreTextDescriptor();
        defer ct_desc.release();

        // Our descriptors have to be in an array
        var ct_desc_arr = [_]*const macos.text.FontDescriptor{ct_desc};
        const desc_arr = try macos.foundation.Array.create(macos.text.FontDescriptor, &ct_desc_arr);
        defer desc_arr.release();

        // Build our collection
        const set = try macos.text.FontCollection.createWithFontDescriptors(desc_arr);
        defer set.release();
        const list = set.createMatchingFontDescriptors();
        defer list.release();

        // Sort our descriptors
        const zig_list = try copyMatchingDescriptors(alloc, list);
        errdefer alloc.free(zig_list);
        sortMatchingDescriptors(&desc, zig_list);

        return DiscoverIterator{
            .alloc = alloc,
            .list = zig_list,
            .variations = desc.variations,
            .i = 0,
        };
    }

    pub fn discoverFallback(
        self: *const CoreText,
        alloc: Allocator,
        collection: *Collection,
        desc: Descriptor,
    ) !DiscoverIterator {
        // If we have a codepoint within the CJK unified ideographs block
        // then we fallback to macOS to find a font that supports it because
        // there isn't a better way manually with CoreText that I can find that
        // properly takes into account system locale.
        //
        // References:
        // - http://unicode.org/charts/PDF/U4E00.pdf
        // - https://chromium.googlesource.com/chromium/src/+/main/third_party/blink/renderer/platform/fonts/LocaleInFonts.md#unified-han-ideographs
        if (desc.codepoint >= 0x4E00 and
            desc.codepoint <= 0x9FFF)
        han: {
            const han = try self.discoverCodepoint(
                collection,
                desc,
            ) orelse break :han;

            // This is silly but our discover iterator needs a slice so
            // we allocate here. This isn't a performance bottleneck but
            // this is something we can optimize very easily...
            const list = try alloc.alloc(*macos.text.FontDescriptor, 1);
            errdefer alloc.free(list);
            list[0] = han;

            return DiscoverIterator{
                .alloc = alloc,
                .list = list,
                .variations = desc.variations,
                .i = 0,
            };
        }

        const it = try self.discover(alloc, desc);

        // If our normal discovery doesn't find anything and we have a specific
        // codepoint, then fallback to using CTFontCreateForString to find a
        // matching font CoreText wants to use. See:
        // https://github.com/ghostty-org/ghostty/issues/2499
        if (it.list.len == 0 and desc.codepoint > 0) codepoint: {
            const ct_desc = try self.discoverCodepoint(
                collection,
                desc,
            ) orelse break :codepoint;

            const list = try alloc.alloc(*macos.text.FontDescriptor, 1);
            errdefer alloc.free(list);
            list[0] = ct_desc;

            return DiscoverIterator{
                .alloc = alloc,
                .list = list,
                .variations = desc.variations,
                .i = 0,
            };
        }

        return it;
    }

    /// Discover a font for a specific codepoint using the CoreText
    /// CTFontCreateForString API.
    fn discoverCodepoint(
        self: *const CoreText,
        collection: *Collection,
        desc: Descriptor,
    ) !?*macos.text.FontDescriptor {
        _ = self;

        if (comptime options.backend.hasFreetype()) {
            // If we have freetype, we can't use CoreText to find a font
            // that supports a specific codepoint because we need to
            // have a CoreText font to be able to do so.
            return null;
        }

        assert(desc.codepoint > 0);

        // Get our original font. This is dependent on the requested style
        // from the descriptor.
        const original = original: {
            // In all the styles below, we try to match it but if we don't
            // we always fall back to some other option. The order matters
            // here.

            if (desc.bold and desc.italic) {
                const entries = collection.faces.get(.bold_italic);
                if (entries.count() > 0) {
                    break :original try collection.getFace(.{ .style = .bold_italic });
                }
            }

            if (desc.bold) {
                const entries = collection.faces.get(.bold);
                if (entries.count() > 0) {
                    break :original try collection.getFace(.{ .style = .bold });
                }
            }

            if (desc.italic) {
                const entries = collection.faces.get(.italic);
                if (entries.count() > 0) {
                    break :original try collection.getFace(.{ .style = .italic });
                }
            }

            break :original try collection.getFace(.{ .style = .regular });
        };

        // We need it in utf8 format
        var buf: [4]u8 = undefined;
        const len = try std.unicode.utf8Encode(
            @intCast(desc.codepoint),
            &buf,
        );

        // We need a CFString
        const str = try macos.foundation.String.createWithBytes(
            buf[0..len],
            .utf8,
            false,
        );
        defer str.release();

        // Get our range length for CTFontCreateForString. It looks like
        // the range uses UTF-16 codepoints and not UTF-32 codepoints.
        const range_len: usize = range_len: {
            var unichars: [2]u16 = undefined;
            const pair = macos.foundation.stringGetSurrogatePairForLongCharacter(
                desc.codepoint,
                &unichars,
            );
            break :range_len if (pair) 2 else 1;
        };

        // Get our font
        const font = original.font.createForString(
            str,
            macos.foundation.Range.init(0, range_len),
        ) orelse return null;
        defer font.release();

        // Do not allow the last resort font to go through. This is the
        // last font used by CoreText if it can't find anything else and
        // only contains replacement characters.
        last_resort: {
            const name_str = font.copyPostScriptName();
            defer name_str.release();

            // If the name doesn't fit in our buffer, then it can't
            // be the last resort font so we break out.
            var name_buf: [64]u8 = undefined;
            const name: []const u8 = name_str.cstring(&name_buf, .utf8) orelse
                break :last_resort;

            // If the name is "LastResort" then we don't want to use it.
            if (std.mem.eql(u8, "LastResort", name)) return null;
        }

        // Get the descriptor
        return font.copyDescriptor();
    }

    fn copyMatchingDescriptors(
        alloc: Allocator,
        list: *macos.foundation.Array,
    ) ![]*macos.text.FontDescriptor {
        var result = try alloc.alloc(*macos.text.FontDescriptor, list.getCount());
        errdefer alloc.free(result);
        for (0..result.len) |i| {
            result[i] = list.getValueAtIndex(macos.text.FontDescriptor, i);

            // We need to retain because once the list is freed it will
            // release all its members.
            result[i].retain();
        }
        return result;
    }

    fn sortMatchingDescriptors(
        desc: *const Descriptor,
        list: []*macos.text.FontDescriptor,
    ) void {
        std.mem.sortUnstable(*macos.text.FontDescriptor, list, desc, struct {
            fn lessThan(
                desc_inner: *const Descriptor,
                lhs: *macos.text.FontDescriptor,
                rhs: *macos.text.FontDescriptor,
            ) bool {
                const lhs_score: Score = .score(desc_inner, lhs);
                const rhs_score: Score = .score(desc_inner, rhs);
                // Higher score is "less" (earlier)
                return lhs_score.int() > rhs_score.int();
            }
        }.lessThan);
    }

    /// We represent our sorting score as a packed struct so that we
    /// can compare scores numerically but build scores symbolically.
    ///
    /// Note that packed structs store their fields from least to most
    /// significant, so the fields here are defined in increasing order
    /// of precedence.
    const Score = packed struct {
        const Backing = @typeInfo(@This()).@"struct".backing_integer.?;

        /// Number of glyphs in the font, if two fonts have identical
        /// scores otherwise then we prefer the one with more glyphs.
        ///
        /// (Number of glyphs clamped at u16 intmax)
        glyph_count: u16 = 0,
        /// A fuzzy match on the style string, less important than
        /// an exact match, and less important than trait matches.
        fuzzy_style: u8 = 0,
        /// Whether the bold-ness of the font matches the descriptor.
        /// This is less important than italic because a font that's italic
        /// when it shouldn't be or not italic when it should be is a bigger
        /// problem (subjectively) than being the wrong weight.
        bold: bool = false,
        /// Whether the italic-ness of the font matches the descriptor.
        /// This is less important than an exact match on the style string
        /// because we want users to be allowed to override trait matching
        /// for the bold/italic/bold italic styles if they want.
        italic: bool = false,
        /// An exact (case-insensitive) match on the style string.
        exact_style: bool = false,
        /// Whether the font is monospace, this is more important than any of
        /// the other fields unless we're looking for a specific codepoint,
        /// in which case that is the most important thing.
        monospace: bool = false,
        /// If we're looking for a codepoint, whether this font has it.
        codepoint: bool = false,

        pub fn int(self: Score) Backing {
            return @bitCast(self);
        }

        fn score(desc: *const Descriptor, ct_desc: *const macos.text.FontDescriptor) Score {
            var self: Score = .{};

            // We always load the font if we can since some things can only be
            // inspected on the font itself. Fonts that can't be loaded score
            // 0 automatically because we don't want a font we can't load.
            const font: *macos.text.Font = macos.text.Font.createWithFontDescriptor(
                ct_desc,
                12,
            ) catch return self;
            defer font.release();

            // We prefer fonts with more glyphs, all else being equal.
            {
                const Type = @TypeOf(self.glyph_count);
                self.glyph_count = std.math.cast(
                    Type,
                    font.getGlyphCount(),
                ) orelse std.math.maxInt(Type);
            }

            // If we're searching for a codepoint, then we
            // prioritize fonts that have that codepoint.
            if (desc.codepoint > 0) {
                // Turn UTF-32 into UTF-16 for CT API
                var unichars: [2]u16 = undefined;
                const pair = macos.foundation.stringGetSurrogatePairForLongCharacter(
                    desc.codepoint,
                    &unichars,
                );
                const len: usize = if (pair) 2 else 1;

                // Get our glyphs
                var glyphs = [2]macos.graphics.Glyph{ 0, 0 };
                self.codepoint = font.getGlyphsForCharacters(
                    unichars[0..len],
                    glyphs[0..len],
                );
            }

            // Get our symbolic traits for the descriptor so we can
            // compare boolean attributes like bold, monospace, etc.
            const symbolic_traits: macos.text.FontSymbolicTraits = traits: {
                const traits = ct_desc.copyAttribute(.traits) orelse break :traits .{};
                defer traits.release();

                const key = macos.text.FontTraitKey.symbolic.key();
                const symbolic = traits.getValue(macos.foundation.Number, key) orelse
                    break :traits .{};

                break :traits macos.text.FontSymbolicTraits.init(symbolic);
            };

            self.monospace = symbolic_traits.monospace;

            // We try to derived data from the font itself, which is generally
            // more reliable than only using the symbolic traits for this.
            const is_bold: bool, const is_italic: bool = derived: {
                // We start with initial guesses based on the symbolic traits,
                // but refine these with more information if we can get it.
                var is_italic = symbolic_traits.italic;
                var is_bold = symbolic_traits.bold;

                // Read the 'head' table out of the font data if it's available.
                if (head: {
                    const tag = macos.text.FontTableTag.init("head");
                    const data = font.copyTable(tag) orelse break :head null;
                    defer data.release();
                    const ptr = data.getPointer();
                    const len = data.getLength();
                    break :head opentype.Head.init(ptr[0..len]) catch |err| {
                        log.warn("error parsing head table: {}", .{err});
                        break :head null;
                    };
                }) |head_| {
                    const head: opentype.Head = head_;
                    is_bold = is_bold or (head.macStyle & 1 == 1);
                    is_italic = is_italic or (head.macStyle & 2 == 2);
                }

                // Read the 'OS/2' table out of the font data if it's available.
                if (os2: {
                    const tag = macos.text.FontTableTag.init("OS/2");
                    const data = font.copyTable(tag) orelse break :os2 null;
                    defer data.release();
                    const ptr = data.getPointer();
                    const len = data.getLength();
                    break :os2 opentype.OS2.init(ptr[0..len]) catch |err| {
                        log.warn("error parsing OS/2 table: {}", .{err});
                        break :os2 null;
                    };
                }) |os2| {
                    is_bold = is_bold or os2.fsSelection.bold;
                    is_italic = is_italic or os2.fsSelection.italic;
                }

                // Check if we have variation axes in our descriptor, if we
                // do then we can derive weight italic-ness or both from them.
                if (font.copyAttribute(.variation_axes)) |axes| variations: {
                    defer axes.release();

                    // Copy the variation values for this instance of the font.
                    // if there are none then we just break out immediately.
                    const values: *macos.foundation.Dictionary =
                        font.copyAttribute(.variation) orelse break :variations;
                    defer values.release();

                    var buf: [1024]u8 = undefined;

                    // If we see the 'ital' value then we ignore 'slnt'.
                    var ital_seen = false;

                    const len = axes.getCount();
                    for (0..len) |i| {
                        const dict = axes.getValueAtIndex(macos.foundation.Dictionary, i);
                        const Key = macos.text.FontVariationAxisKey;
                        const cf_id = dict.getValue(Key.identifier.Value(), Key.identifier.key()).?;
                        const cf_name = dict.getValue(Key.name.Value(), Key.name.key()).?;
                        const cf_def = dict.getValue(Key.default_value.Value(), Key.default_value.key()).?;

                        const name_str = cf_name.cstring(&buf, .utf8) orelse "";

                        // Default value
                        var def: f64 = 0;
                        _ = cf_def.getValue(.double, &def);
                        // Value in this font
                        var val: f64 = def;
                        if (values.getValue(
                            macos.foundation.Number,
                            cf_id,
                        )) |cf_val| _ = cf_val.getValue(.double, &val);

                        if (std.mem.eql(u8, "wght", name_str)) {
                            // Somewhat subjective threshold, we consider fonts
                            // bold if they have a 'wght' set greater than 600.
                            is_bold = val > 600;
                            continue;
                        }
                        if (std.mem.eql(u8, "ital", name_str)) {
                            is_italic = val > 0.5;
                            ital_seen = true;
                            continue;
                        }
                        if (!ital_seen and std.mem.eql(u8, "slnt", name_str)) {
                            // Arbitrary threshold of anything more than a 5
                            // degree clockwise slant is considered italic.
                            is_italic = val <= -5.0;
                            continue;
                        }
                    }
                }

                break :derived .{ is_bold, is_italic };
            };

            self.bold = desc.bold == is_bold;
            self.italic = desc.italic == is_italic;

            // Get the style string from the font.
            var style_str_buf: [128]u8 = undefined;
            const style_str: []const u8 = style_str: {
                const style = ct_desc.copyAttribute(.style_name) orelse
                    break :style_str "";
                defer style.release();

                break :style_str style.cstring(&style_str_buf, .utf8) orelse "";
            };

            // The first string in this slice will be used for the exact match,
            // and for the fuzzy match, all matching substrings will increase
            // the rank.
            const desired_styles: []const [:0]const u8 = desired: {
                if (desc.style) |s| break :desired &.{s};

                // If we don't have an explicitly desired style name, we base
                // it on the bold and italic properties, this isn't ideal since
                // fonts may use style names other than these, but it helps in
                // some edge cases.
                if (desc.bold) {
                    if (desc.italic) break :desired &.{ "bold italic", "bold", "italic", "oblique" };
                    break :desired &.{ "bold", "upright" };
                } else if (desc.italic) {
                    break :desired &.{ "italic", "regular", "oblique" };
                }
                break :desired &.{ "regular", "upright" };
            };

            self.exact_style = std.ascii.eqlIgnoreCase(
                style_str,
                desired_styles[0],
            );
            // Our "fuzzy match" score is 0 if the desired style isn't present
            // in the string, otherwise we give higher priority for styles that
            // have fewer characters not in the desired_styles list.
            const fuzzy_type = @TypeOf(self.fuzzy_style);
            self.fuzzy_style = @intCast(style_str.len);
            for (desired_styles) |s| {
                if (std.ascii.indexOfIgnoreCase(style_str, s) != null) {
                    self.fuzzy_style -|= @intCast(s.len);
                }
            }
            self.fuzzy_style = std.math.maxInt(fuzzy_type) -| self.fuzzy_style;

            return self;
        }
    };

    pub const DiscoverIterator = struct {
        alloc: Allocator,
        list: []const *macos.text.FontDescriptor,
        variations: []const Variation,
        i: usize,

        pub fn deinit(self: *DiscoverIterator) void {
            for (self.list) |desc| {
                desc.release();
            }
            self.alloc.free(self.list);
            self.* = undefined;
        }

        pub fn next(self: *DiscoverIterator) !?DeferredFace {
            if (self.i >= self.list.len) return null;

            // Get our descriptor. We need to remove the character set
            // limitation because we may have used that to filter but we
            // don't want it anymore because it'll restrict the characters
            // available.
            const desc = desc: {
                // We create a copy, overwriting the character set attribute.
                const attrs = try macos.foundation.MutableDictionary.create(0);
                defer attrs.release();

                attrs.setValue(
                    macos.text.FontAttribute.character_set.key(),
                    macos.c.kCFNull,
                );

                break :desc try macos.text.FontDescriptor.createCopyWithAttributes(
                    self.list[self.i],
                    @ptrCast(attrs),
                );
            };
            defer desc.release();

            // Create our font. We need a size to initialize it so we use size
            // 12 but we will alter the size later.
            const font = try macos.text.Font.createWithFontDescriptor(desc, 12);
            errdefer font.release();

            // Increment after we return
            defer self.i += 1;

            return DeferredFace{
                .ct = .{
                    .font = font,
                    .variations = self.variations,
                },
            };
        }
    };
};

test "descriptor hash" {
    const testing = std.testing;

    var d: Descriptor = .{};
    try testing.expect(d.hashcode() != 0);
}

test "descriptor hash family names" {
    const testing = std.testing;

    var d1: Descriptor = .{ .family = "A" };
    var d2: Descriptor = .{ .family = "B" };
    try testing.expect(d1.hashcode() != d2.hashcode());
}

test "fontconfig" {
    if (options.backend != .fontconfig_freetype) return error.SkipZigTest;

    const testing = std.testing;
    const alloc = testing.allocator;

    var fc = Fontconfig.init();
    defer fc.deinit();
    var it = try fc.discover(alloc, .{ .family = "monospace", .size = 12 });
    defer it.deinit();
}

test "fontconfig codepoint" {
    if (options.backend != .fontconfig_freetype) return error.SkipZigTest;

    const testing = std.testing;
    const alloc = testing.allocator;

    var fc = Fontconfig.init();
    defer fc.deinit();
    var it = try fc.discover(alloc, .{ .codepoint = 'A', .size = 12 });
    defer it.deinit();

    // The first result should have the codepoint. Later ones may not
    // because fontconfig returns all fonts sorted.
    var face = (try it.next()).?;
    defer face.deinit();
    try testing.expect(face.hasCodepoint('A', null));

    // Should have other codepoints too
    try testing.expect(face.hasCodepoint('B', null));
}

test "coretext" {
    if (options.backend != .coretext and options.backend != .coretext_freetype)
        return error.SkipZigTest;

    const testing = std.testing;
    const alloc = testing.allocator;

    var ct = CoreText.init();
    defer ct.deinit();
    var it = try ct.discover(alloc, .{ .family = "Monaco", .size = 12 });
    defer it.deinit();
    var count: usize = 0;
    while (try it.next()) |_| {
        count += 1;
    }
    try testing.expect(count > 0);
}

test "coretext codepoint" {
    if (options.backend != .coretext and options.backend != .coretext_freetype)
        return error.SkipZigTest;

    const testing = std.testing;
    const alloc = testing.allocator;

    var ct = CoreText.init();
    defer ct.deinit();
    var it = try ct.discover(alloc, .{ .codepoint = 'A', .size = 12 });
    defer it.deinit();

    // The first result should have the codepoint. Later ones may not
    // because fontconfig returns all fonts sorted.
    const face = (try it.next()).?;
    try testing.expect(face.hasCodepoint('A', null));

    // Should have other codepoints too
    try testing.expect(face.hasCodepoint('B', null));
}

test "coretext sorting" {
    if (options.backend != .coretext and options.backend != .coretext_freetype)
        return error.SkipZigTest;

    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!//
    // FIXME: Disabled for now because SF Pro is not available in CI
    //        The solution likely involves directly testing that the
    //        `sortMatchingDescriptors` function sorts a bundled test
    //        font correctly, instead of relying on the system fonts.
    if (true) return error.SkipZigTest;
    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!//

    const testing = std.testing;
    const alloc = testing.allocator;

    var ct = CoreText.init();
    defer ct.deinit();

    // We try to get a Regular, Italic, Bold, & Bold Italic version of SF Pro,
    // which should be installed on all Macs, and has many styles which makes
    // it a good test, since there will be many results for each discovery.

    // Regular
    {
        var it = try ct.discover(alloc, .{
            .family = "SF Pro",
            .size = 12,
        });
        defer it.deinit();
        const res = (try it.next()).?;
        var buf: [1024]u8 = undefined;
        const name = try res.name(&buf);
        try testing.expectEqualStrings("SF Pro Regular", name);
    }

    // Regular Italic
    //
    // NOTE: This makes sure that we don't accidentally prefer "Thin Italic",
    //       which we previously did, because it has a shorter name.
    {
        var it = try ct.discover(alloc, .{
            .family = "SF Pro",
            .size = 12,
            .italic = true,
        });
        defer it.deinit();
        const res = (try it.next()).?;
        var buf: [1024]u8 = undefined;
        const name = try res.name(&buf);
        try testing.expectEqualStrings("SF Pro Regular Italic", name);
    }

    // Bold
    {
        var it = try ct.discover(alloc, .{
            .family = "SF Pro",
            .size = 12,
            .bold = true,
        });
        defer it.deinit();
        const res = (try it.next()).?;
        var buf: [1024]u8 = undefined;
        const name = try res.name(&buf);
        try testing.expectEqualStrings("SF Pro Bold", name);
    }

    // Bold Italic
    {
        var it = try ct.discover(alloc, .{
            .family = "SF Pro",
            .size = 12,
            .bold = true,
            .italic = true,
        });
        defer it.deinit();
        const res = (try it.next()).?;
        var buf: [1024]u8 = undefined;
        const name = try res.name(&buf);
        try testing.expectEqualStrings("SF Pro Bold Italic", name);
    }
}

/// DirectWrite font discovery for Windows.
///
/// Uses the IDWriteFactory COM interface to enumerate system fonts by family
/// name. FreeType is used for the actual glyph rasterization; DirectWrite
/// only provides the font file path so FreeType can open the file.
///
/// COM must be initialized on the calling thread (CoInitializeEx) before
/// calling init(). On Windows, COM is typically initialized during
/// application startup.
pub const DirectWrite = struct {
    /// Opaque handle to the IDWriteFactory COM object.
    /// We store it as a raw pointer and call through the vtable.
    factory: *anyopaque,

    const windows = @import("std").os.windows;

    /// GUID for IDWriteFactory {B859EE5A-D838-4B5B-A2E8-1ADC7D93DB48}
    const IID_IDWriteFactory = windows.GUID{
        .Data1 = 0xB859EE5A,
        .Data2 = 0xD838,
        .Data3 = 0x4B5B,
        .Data4 = .{ 0xA2, 0xE8, 0x1A, 0xDC, 0x7D, 0x93, 0xDB, 0x48 },
    };

    const DWRITE_FACTORY_TYPE_SHARED: windows.DWORD = 0;

    extern "dwrite" fn DWriteCreateFactory(
        factory_type: windows.DWORD,
        riid: *const windows.GUID,
        factory: **anyopaque,
    ) callconv(.winapi) windows.HRESULT;

    /// Initialize the DirectWrite discovery engine by creating a shared
    /// IDWriteFactory. Returns an error if DirectWrite is not available
    /// (Windows 7 or later required).
    pub fn init() !DirectWrite {
        var factory: *anyopaque = undefined;
        const hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            &IID_IDWriteFactory,
            &factory,
        );
        if (hr != windows.S_OK) return error.DirectWriteInitFailed;
        return .{ .factory = factory };
    }

    /// Release the IDWriteFactory COM object.
    pub fn deinit(self: *DirectWrite) void {
        // Call IUnknown::Release via the vtable.
        // IDWriteFactory vtable: [0]=QueryInterface, [1]=AddRef, [2]=Release
        const vtable = @as(*const *const extern struct {
            QueryInterface: *const anyopaque,
            AddRef: *const anyopaque,
            Release: *const fn (*anyopaque) callconv(.winapi) windows.ULONG,
        }, @ptrCast(self.factory)).*;
        _ = vtable.Release(self.factory);
        self.* = undefined;
    }

    /// Discover fonts matching the given descriptor. Returns an iterator
    /// that yields DeferredFace values backed by font file paths.
    pub fn discover(
        self: *const DirectWrite,
        alloc: Allocator,
        desc: Descriptor,
    ) !DiscoverIterator {
        return DiscoverIterator.init(alloc, self.factory, desc);
    }

    pub fn discoverFallback(
        self: *const DirectWrite,
        alloc: Allocator,
        collection: *Collection,
        desc: Descriptor,
    ) !DiscoverIterator {
        _ = collection;
        return try self.discover(alloc, desc);
    }

    /// Iterator returned by DirectWrite.discover(). Each call to next()
    /// returns one DeferredFace with a file path for FreeType to open.
    pub const DiscoverIterator = struct {
        alloc: Allocator,
        /// Null-terminated font file paths discovered by DirectWrite.
        paths: [][:0]const u8,
        variations: []const Variation,
        i: usize,

        fn init(
            alloc: Allocator,
            factory: *anyopaque,
            desc: Descriptor,
        ) !DiscoverIterator {
            _ = factory;

            // Enumerate font file paths via DirectWrite COM calls.
            // For now we use a simplified implementation that calls
            // the system font enumeration API and filters by family name.
            var paths = std.ArrayList([:0]const u8).init(alloc);
            errdefer {
                for (paths.items) |p| alloc.free(p);
                paths.deinit();
            }

            try enumerateFontPaths(alloc, desc, &paths);

            return .{
                .alloc = alloc,
                .paths = try paths.toOwnedSlice(),
                .variations = desc.variations,
                .i = 0,
            };
        }

        pub fn deinit(self: *DiscoverIterator) void {
            for (self.paths) |p| self.alloc.free(p);
            self.alloc.free(self.paths);
            self.* = undefined;
        }

        pub fn next(self: *DiscoverIterator) !?DeferredFace {
            if (self.i >= self.paths.len) return null;
            defer self.i += 1;

            return DeferredFace{
                .dw = .{
                    .alloc = self.alloc,
                    .path = self.paths[self.i],
                    .index = 0,
                    .variations = self.variations,
                },
            };
        }
    };

    /// Windows Registry API declarations for font enumeration.
    const HKEY = *anyopaque;
    const HKLM: HKEY = @ptrFromInt(0x80000002);
    const KEY_READ: u32 = 0x20019;
    const REG_SZ: u32 = 1;
    const ERROR_NO_MORE_ITEMS: i32 = 259;

    extern "advapi32" fn RegOpenKeyExW(
        hKey: HKEY,
        lpSubKey: [*:0]const u16,
        ulOptions: u32,
        samDesired: u32,
        phkResult: *HKEY,
    ) callconv(.winapi) i32;

    extern "advapi32" fn RegCloseKey(hKey: HKEY) callconv(.winapi) i32;

    extern "advapi32" fn RegEnumValueW(
        hKey: HKEY,
        dwIndex: u32,
        lpValueName: [*]u16,
        lpcchValueName: *u32,
        lpReserved: ?*u32,
        lpType: ?*u32,
        lpData: ?[*]u8,
        lpcbData: ?*u32,
    ) callconv(.winapi) i32;

    /// Enumerate font file paths from the Windows Fonts registry key matching
    /// the given descriptor. This uses the HKLM registry approach as a
    /// pragmatic font enumeration method that works without full DirectWrite
    /// COM interface implementation. A complete DirectWrite collection API
    /// can be layered in later without changing the iterator interface.
    fn enumerateFontPaths(
        alloc: Allocator,
        desc: Descriptor,
        paths: *std.ArrayList([:0]const u8),
    ) !void {
        // Open HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts
        const key_path = std.unicode.utf8ToUtf16LeStringLiteral(
            "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
        );
        var hkey: HKEY = undefined;
        const open_result = RegOpenKeyExW(HKLM, key_path, 0, KEY_READ, &hkey);
        if (open_result != 0) return; // Registry key unavailable; skip gracefully
        defer _ = RegCloseKey(hkey);

        // Get the Windows Fonts directory path.
        const sysroot = std.process.getEnvVarOwned(alloc, "SYSTEMROOT") catch
            try alloc.dupe(u8, "C:\\Windows");
        defer alloc.free(sysroot);

        var fonts_dir_buf: [std.fs.max_path_bytes]u8 = undefined;
        const fonts_dir = try std.fmt.bufPrint(
            &fonts_dir_buf,
            "{s}\\Fonts",
            .{sysroot},
        );

        // Enumerate registry values. Each value maps a font name to a filename.
        var value_index: u32 = 0;
        var name_buf: [512]u16 = undefined;
        var data_buf: [1024]u8 = undefined;

        while (true) {
            var name_len: u32 = @intCast(name_buf.len);
            var data_type: u32 = 0;
            var data_len: u32 = @intCast(data_buf.len);

            const enum_result = RegEnumValueW(
                hkey,
                value_index,
                &name_buf,
                &name_len,
                null,
                &data_type,
                &data_buf,
                &data_len,
            );
            value_index += 1;

            if (enum_result == ERROR_NO_MORE_ITEMS) break;
            if (enum_result != 0) continue;
            if (data_type != REG_SZ) continue; // only string values

            // Convert the value name (font display name) to UTF-8 for matching.
            const name_utf8 = std.unicode.utf16LeToUtf8Alloc(
                alloc,
                name_buf[0..name_len],
            ) catch continue;
            defer alloc.free(name_utf8);

            // Check whether this font entry matches the requested family.
            const matches = if (desc.family) |family| blk: {
                // Case-insensitive substring search: does the font name contain
                // the requested family string?
                const haystack = name_utf8;
                const needle = family;
                if (needle.len > haystack.len) break :blk false;
                var i: usize = 0;
                while (i <= haystack.len - needle.len) : (i += 1) {
                    if (std.ascii.eqlIgnoreCase(haystack[i..][0..needle.len], needle)) {
                        break :blk true;
                    }
                }
                break :blk false;
            } else true; // no family filter — include all
            if (!matches) continue;

            // The registry value data is a UTF-16LE filename string (bytes).
            // bytesAsSlice requires an even byte count; mask off any trailing
            // odd byte that Windows might report for a truncated read.
            const data_len_even = data_len & ~@as(u32, 1);
            const filename_utf16 = std.mem.bytesAsSlice(u16, data_buf[0..data_len_even]);
            // Strip null terminator if present.
            const filename_utf16_trimmed = if (filename_utf16.len > 0 and
                filename_utf16[filename_utf16.len - 1] == 0)
                filename_utf16[0 .. filename_utf16.len - 1]
            else
                filename_utf16;

            const filename_utf8 = std.unicode.utf16LeToUtf8Alloc(
                alloc,
                filename_utf16_trimmed,
            ) catch continue;
            defer alloc.free(filename_utf8);

            // Build the full font file path.
            const full_path = if (std.fs.path.isAbsolute(filename_utf8))
                try alloc.dupeZ(u8, filename_utf8)
            else
                try std.fmt.allocPrintZ(alloc, "{s}\\{s}", .{ fonts_dir, filename_utf8 });
            errdefer alloc.free(full_path);

            // Verify the file exists before adding to the list.
            std.fs.accessAbsolute(full_path, .{}) catch {
                alloc.free(full_path);
                continue;
            };

            try paths.append(full_path);
        }
    }
};
