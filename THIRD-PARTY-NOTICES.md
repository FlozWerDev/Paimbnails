# Third-Party Notices — Paimbnails

All features listed below are **independent implementations written from
scratch for Paimbnails**, with two stated exceptions: **Search History
(§1)**, which ports logic from its MIT-licensed original and says so in
its section, and **Separate Dual Icons (§3)**, inspired by an unlicensed
original and rewritten from its behavior spec (see its section). Otherwise no
third-party code was copied. The credits exist
because the *idea* was inspired by another creator, or because the feature
interoperates with third-party content that remains owned by its publishers.

If you are one of these authors and want the wording changed, open an issue
or reach out on Discord and it will be fixed.

---

## 1. Search History — MIT License (hiimjasmine00)

Feature: Search History (`src/features/search-history/`).
Based on "Search History" by **hiimjasmine00**
(Geode id `hiimjustin000.search_history`,
source: https://github.com/hiimjasmine00/SearchHistory).

The original mod is distributed under the MIT License. The Paimbnails
feature is a port that retains the upstream capture/restore logic
(`SearchHistory.{hpp,cpp}`, filter snapshot/restore in
`hooks/LevelSearchLayer.cpp`) with Paimbnails UI (`ui/SearchHistoryPopup`)
and persistence. Because upstream code was adapted, the MIT attribution
is reproduced here so the credit travels with every copy, as the license
requires:

```
MIT License

Copyright (c) 2024-2026 hiimjasmine00

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

The MIT license permits closed-source redistribution and does not require
source disclosure or same-license redistribution. It grants no trademark
rights.

---

## 2. Message Notifications — Apache License 2.0 (BlueToadMaker)

Feature: Message Notifications (`src/features/message-notifications/`).
Idea inspired by "Message-Notification" by **BlueToadMaker**
(`bluetoadmaker.messagenotification` on the Geode index,
source: https://github.com/BlueToadMakerr/Message-Notification).

The original mod is distributed under the Apache License, Version 2.0. The
Paimbnails implementation was written from scratch (own polling thread,
seeding logic, settings, parsers covering only the keys it needs) and talks
to RobTop's own public endpoints (`getGJMessages20.php`,
`getGJFriendRequests20.php` — the same ones the game client uses; endpoint
paths, key numbers and the GD secret are protocol facts, not creative
expression). To comply with Apache-2.0 §4 for any portion that could be
seen as derived, this file reproduces the license, the feature headers mark
the files as independently implemented, and the credit above is kept in
`about.md`, `mod.json` and the source header.

Upstream note: the original repository ships the stock Apache-2.0 boilerplate
with the appendix copyright line left unfilled and no per-file headers, so
attribution here names BlueToadMaker / BlueToadMakerr manually.

```
                                 Apache License
                           Version 2.0, January 2004
                        http://www.apache.org/licenses/

   TERMS AND CONDITIONS FOR USE, REPRODUCTION, AND DISTRIBUTION

   1. Definitions.

      "License" shall mean the terms and conditions for use, reproduction,
      and distribution as defined by Sections 1 through 9 of this document.

      "Licensor" shall mean the copyright owner or entity authorized by
      the copyright owner that is granting the License.

      "Legal Entity" shall mean the union of the acting entity and all
      other entities that control, are controlled by, or are under common
      control with that entity. For the purposes of this definition,
      "control" means (i) the power, direct or indirect, to cause the
      direction or management of such entity, whether by contract or
      otherwise, or (ii) ownership of fifty percent (50%) or more of the
      outstanding shares, or (iii) beneficial ownership of such entity.

      "You" (or "Your") shall mean an individual or Legal Entity
      exercising permissions granted by this License.

      "Source" form shall mean the preferred form for making modifications,
      including but not limited to software source code, documentation
      source, and configuration files.

      "Object" form shall mean any form resulting from mechanical
      transformation or translation of a Source form, including but
      not limited to compiled object code, generated documentation,
      and conversions to other media types.

      "Work" shall mean the work of authorship, whether in Source or
      Object form, made available under the License, as indicated by a
      copyright notice that is included in or attached to the work
      (an example is provided in the Appendix below).

      "Derivative Works" shall mean any work, whether in Source or Object
      form, that is based on (or derived from) the Work and for which the
      editorial revisions, annotations, elaborations, or other modifications
      represent, as a whole, an original work of authorship. For the purposes
      of this License, Derivative Works shall not include works that remain
      separable from, or merely link (or bind by name) to the interfaces of,
      the Work and Derivative Works thereof.

      "Contribution" shall mean any work of authorship, including
      the original version of the Work and any modifications or additions
      to that Work or Derivative Works thereof, that is intentionally
      submitted to Licensor for inclusion in the Work by the copyright owner
      or by an individual or Legal Entity authorized to submit on behalf of
      the copyright owner. For the purposes of this definition, "submitted"
      means any form of electronic, verbal, or written communication sent
      to the Licensor or its representatives, including but not limited to
      communication on electronic mailing lists, source code control systems,
      and issue tracking systems that are managed by, or on behalf of, the
      Licensor for the purpose of discussing and improving the Work, but
      excluding communication that is conspicuously marked or otherwise
      designated in writing by the copyright owner as "Not a Contribution."

      "Contributor" shall mean Licensor and any individual or Legal Entity
      on behalf of whom a Contribution has been received by Licensor and
      subsequently incorporated within the Work.

   2. Grant of Copyright License. Subject to the terms and conditions of
      this License, each Contributor hereby grants to You a perpetual,
      worldwide, non-exclusive, no-charge, royalty-free, irrevocable
      copyright license to reproduce, prepare Derivative Works of,
      publicly display, publicly perform, sublicense, and distribute the
      Work and such Derivative Works in Source or Object form.

   3. Grant of Patent License. Subject to the terms and conditions of
      this License, each Contributor hereby grants to You a perpetual,
      worldwide, non-exclusive, no-charge, royalty-free, irrevocable
      (except as stated in this section) patent license to make, have made,
      use, offer to sell, sell, import, and otherwise transfer the Work,
      where such license applies only to those patent claims licensable
      by such Contributor that are necessarily infringed by their
      Contribution(s) alone or by combination of their Contribution(s)
      with the Work to which such Contribution(s) was submitted. If You
      institute patent litigation against any entity (including a
      cross-claim or counterclaim in a lawsuit) alleging that the Work
      or a Contribution incorporated within the Work constitutes direct
      or contributory patent infringement, then any patent licenses
      granted to You under this License for that Work shall terminate
      as of the date such litigation is filed.

   4. Redistribution. You may reproduce and distribute copies of the
      Work or Derivative Works thereof in any medium, with or without
      modifications, and in Source or Object form, provided that You
      meet the following conditions:

      (a) You must give any other recipients of the Work or
          Derivative Works a copy of this License; and

      (b) You must cause any modified files to carry prominent notices
          stating that You changed the files; and

      (c) You must retain, in the Source form of any Derivative Works
          that You distribute, all copyright, patent, trademark, and
          attribution notices from the Source form of the Work,
          excluding those notices that do not pertain to any part of
          the Derivative Works; and

      (d) If the Work includes a "NOTICE" text file as part of its
          distribution, then any Derivative Works that You distribute must
          include a readable copy of the attribution notices contained
          within such NOTICE file, excluding those notices that do not
          pertain to any part of the Derivative Works, in at least one
          of the following places: within a NOTICE text file distributed
          as part of the Derivative Works; within the Source form or
          documentation, if provided along with the Derivative Works; or,
          within a display generated by the Derivative Works, if and
          wherever such third-party notices normally appear. The contents
          of the NOTICE file are for informational purposes only and
          do not modify the License. You may add Your own attribution
          notices within Derivative Works that You distribute, alongside
          or as an addendum to the NOTICE text from the Work, provided
          that such additional attribution notices cannot be construed
          as modifying the License.

      You may add Your own copyright statement to Your modifications and
      may provide additional or different license terms and conditions
      for use, reproduction, or distribution of Your modifications, or
      for any such Derivative Works as a whole, provided Your use,
      reproduction, and distribution of the Work otherwise complies with
      the conditions stated in this License.

   5. Submission of Contributions. Unless You explicitly state otherwise,
      any Contribution intentionally submitted for inclusion in the Work
      by You to the Licensor shall be under the terms and conditions of
      this License, without any additional terms or conditions.
      Notwithstanding the above, nothing herein shall supersede or modify
      the terms of any separate license agreement you may have executed
      with Licensor regarding such Contributions.

   6. Trademarks. This License does not grant permission to use the trade
      names, trademarks, service marks, or product names of the Licensor,
      except as required for reasonable and customary use in describing the
      origin of the Work and reproducing the content of the NOTICE file.

   7. Disclaimer of Warranty. Unless required by applicable law or
      agreed to in writing, Licensor provides the Work (and each
      Contributor provides its Contributions) on an "AS IS" BASIS,
      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
      implied, including, without limitation, any warranties or conditions
      of TITLE, NON-INFRINGEMENT, MERCHANTABILITY, or FITNESS FOR A
      PARTICULAR PURPOSE. You are solely responsible for determining the
      appropriateness of using or redistributing the Work and assume any
      risks associated with Your exercise of permissions under this License.

   8. Limitation of Liability. In no event and under no legal theory,
      whether in tort (including negligence), contract, or otherwise,
      unless required by applicable law (such as deliberate and grossly
      negligent acts) or agreed to in writing, shall any Contributor be
      liable to You for damages, including any direct, indirect, special,
      incidental, or consequential damages of any character arising as a
      result of this License or out of the use or inability to use the
      Work (including but not limited to damages for loss of goodwill,
      work stoppage, computer failure or malfunction, or any and all
      other commercial damages or losses), even if such Contributor
      has been advised of the possibility of such damages.

   9. Accepting Warranty or Additional Liability. While redistributing
      the Work or Derivative Works thereof, You may choose to offer,
      and charge a fee for, acceptance of support, warranty, indemnity,
      or other liability obligations and/or rights consistent with this
      License. However, in accepting such obligations, You may act only
      on Your own behalf and on Your sole responsibility, not on behalf
      of any other Contributor, and only if You agree to indemnify,
      defend, and hold each Contributor harmless for any liability
      incurred by, or claims asserted against, such Contributor by reason
      of your accepting any such warranty or additional liability.

   END OF TERMS AND CONDITIONS

   APPENDIX: How to apply the Apache License to your work.

      To apply the Apache License to your work, attach the following
      boilerplate notice, with the fields enclosed by brackets "[]"
      replaced with your own identifying information. (Don't include
      the brackets!)  The text should be enclosed in the appropriate
      comment syntax for the file format. We also recommend that a
      file or class name and description of purpose be included on the
      same "printed page" as the copyright notice for easier
      identification within third-party archives.

   Copyright [yyyy] [name of copyright owner]

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
```

Full license text: http://www.apache.org/licenses/LICENSE-2.0

---

## 3. Courtesy credits — unlicensed works (all rights reserved)

The following originals state **no license** (no LICENSE/COPYING/UNLICENSE,
no SPDX identifier, no license headers found in the repository snapshots
reviewed; Ecuet's repository license status is still unconfirmed, so it is
treated the same way). Default exclusive copyright applies, so Paimbnails
does **not** copy their code — the implementations below are clean-room,
idea-only recreations, including Separate Dual Icons, whose rewrite is
documented in its entry below. The credits are a courtesy, not a license
grant,
and grant no trademark rights.

- **Separate Dual Icons** by **Weebify**
  (`weebify.separate_dual_icons`, source:
  https://github.com/Weebifying/separate-dual-icons-geode), itself derived
  from the original 2.1-era SeparateDualIcons by **Alphalaneous**.
  STATUS (2026-09-20): a source-level audit found that the Paimbnails
  feature in `src/features/separate-dual/` still carried expression
  ported from this unlicensed original, so it was **rewritten from the
  behavior spec the same day** (same user-visible behavior; own
  architecture: slot-table storage, snapshot kit exchange, data-table
  trail/ship-fire tuning, table-driven garage picks). No code from the
  original remains. Deliberately preserved: the on-disk save schema (key
  names and lasttype codes, so existing P2 kits keep working), the Geode
  hook signatures and node IDs the feature interoperates with, and GD
  facts no one owns (API names, asset filenames, numeric tuning).
  Residual note: behavior-compatible hooks inevitably make the same GD
  API calls in a similar order — that is functional convergence, not
  copied expression.
- **Mod Previews** by **Alphalaneous** (`alphalaneous.mod_previews`,
  source: https://github.com/Alphalaneous/Mod-Previews). Paimbnails feature:
  `src/features/mod-previews/` (own URL parsing, branch probing and gallery;
  only the uncopyrightable `previews/preview-N.png` convention is shared).
  Preview images belong to each mod's own repository authors and are only
  displayed, never redistributed. A 2026-09-20 source audit confirmed no
  copied code (verified against the live upstream). Note: an unrelated
  later mod with the same name by Cheeseworks is GPL-3.0 — Paimbnails
  copies nothing from it, so no copyleft applies.
- **Custom Cursor** by **Ecuet** (`ecuet.custom-cursor`, source:
  https://github.com/Ecuet/Custom-Cursor; no LICENSE/LICENSE.md on main
  or master (checked 2026-09-15; API rate-limited), so treated as all
  rights reserved). Paimbnails reuses only the
  click-state tracking idea (`src/features/cursor/hooks/CursorHook.cpp`,
  `src/features/cursor/services/CursorShopClient.hpp`); the shop client
  was written against the store sites' own pages, and no Ecuet code was
  knowingly reused. A source diff against the upstream was performed on
  2026-09-20 and confirmed idea-only reuse (no shared identifiers,
  blocks or assets).

---

## 4. Recoloring approach — PackGen / TexturePackWeb (informal grant)

Texture Studio (`src/features/texture-studio/`) implements its own
luminance-tinting pipeline (standard Rec.601 weights), overlay order and
metadata builders from scratch. It is fully self-contained: it works
offline from the game resources already on disk, with no mirror, no
download and no third-party artifact shipped or fetched — so no
license-copy obligation is triggered.

- The recoloring idea was inspired by **PackGen** by **Asterveila**
  (https://packgenweb.pages.dev/) — closed source, no license text.
- PackGen itself credits the original implementation/idea:
  **TexturePackWeb** by **ravexcode** (https://github.com/ravexcode/TexturePackWeb).
  That repository carries no formal license file; its site footer grants, in
  Spanish: "© Pagina de libre uso y edicion. Puedes ver el codigo (si te
  atreves) y editarlo al gusto para crear una pagina web" (free use and
  editing). There are no attribution-text, license-copy, source-disclosure,
  copyleft or trademark clauses because there is no formal license — only
  this informal grant. The base Geometry Dash art belongs to RobTop.
- Generated packs credit the approach in their README.

---

## 5. Cursor artworks — rw-designer.com / custom-cursor.com

The cursor shop (`src/features/cursor/`) browses **rw-designer.com** and
**custom-cursor.com**. Every cursor artwork belongs to those sites and their
respective authors. Listings link back to the original pages ("Ver original"
button, including loose cursors which link to the store section),
thumbnails are only previewed, and files download exclusively when the user
explicitly installs something — the catalogue is never bulk-scraped. Use is
subject to each site's own Terms of Service.

---

## 6. Dependencies & service interop (no code copied)

- **More Icons** by **hiimjasmine00** (Geode id
  `hiimjustin000.more_icons`): a declared dependency in `mod.json`
  ("Requires the More Icons mod"). Paimbnails uses only its public API
  (`hiimjustin000.more_icons/include/MoreIcons.hpp`, resolved at build
  time from the dependency, never vendored) to register icons
  and resolve equipped ones (`src/features/global-icon/`,
  `src/features/icon-maker/services/MoreIconsBridge.cpp`). The dependency
  ships separately through the Geode index; Paimbnails distributes none of
  its code.

---

## 7. How to add future "inspired by" work

1. Write the implementation from scratch — do not copy code, assets or text.
2. Put a header comment naming the original (mod name, author, Geode index
   or repo URL) and stating the implementation is independent.
3. Add a user-facing credit in `mod.json` (feature description) and
   `about.md` (this section's list).
4. If the original has a license requiring a notice or license copy
   (MIT, Apache-2.0, …), reproduce it in this file.
5. If the feature depends on third-party hosting or artworks, link back to
   the original pages and offer a mirror/override setting where feasible.
