# Changelog

## [0.28.0](https://github.com/metasequoiaime/msime-engine/compare/v0.27.2...v0.28.0) (2026-09-28)


### Features

* **input:** decode candidates by caret prefix ([#197](https://github.com/metasequoiaime/msime-engine/issues/197)) ([bc46f27](https://github.com/metasequoiaime/msime-engine/commit/bc46f2762de3b9712c1d225a804972d7eed34b40))
* **input:** mark sentence association candidates ([#187](https://github.com/metasequoiaime/msime-engine/issues/187)) ([01358ff](https://github.com/metasequoiaime/msime-engine/commit/01358ff20d848db6fbc7ed65c994c7f5333c2adb))


### Bug Fixes

* **dictionary:** isolate journal connections per thread ([#188](https://github.com/metasequoiaime/msime-engine/issues/188)) ([a9b9f09](https://github.com/metasequoiaime/msime-engine/commit/a9b9f092219505166c927843762b650d1e0e501d))
* **dictionary:** wait for concurrent write commits ([#185](https://github.com/metasequoiaime/msime-engine/issues/185)) ([66d63a2](https://github.com/metasequoiaime/msime-engine/commit/66d63a293831802e35b33decd570f867dd2a69bc))
* **input:** learn selected sentence candidates ([#193](https://github.com/metasequoiaime/msime-engine/issues/193)) ([88608bd](https://github.com/metasequoiaime/msime-engine/commit/88608bd2c9fbcd6c302ac3014122a27c5b70a08d))
* **input:** persist segmented online candidates ([#196](https://github.com/metasequoiaime/msime-engine/issues/196)) ([2f606cb](https://github.com/metasequoiaime/msime-engine/commit/2f606cb2ab195573e12f386d12697a50305e4672))
* **input:** preserve manual segmentation in cloud queries ([#189](https://github.com/metasequoiaime/msime-engine/issues/189)) ([b049eac](https://github.com/metasequoiaime/msime-engine/commit/b049eac3555fa1ef2f7546fe788f8dd442189298))
* **quanpin:** normalize umlaut spelling aliases ([#195](https://github.com/metasequoiaime/msime-engine/issues/195)) ([6f3370f](https://github.com/metasequoiaime/msime-engine/commit/6f3370fdcf902d2cf9d9d8fc5c5b30ddfcf7b663))
* **quanpin:** preserve tuned alternative ranking ([#190](https://github.com/metasequoiaime/msime-engine/issues/190)) ([3daaf16](https://github.com/metasequoiaime/msime-engine/commit/3daaf1643bc8815515e8595ab5f2980663fcc67d))
* **shuangpin:** accept yo as a complete syllable ([#191](https://github.com/metasequoiaime/msime-engine/issues/191)) ([d810978](https://github.com/metasequoiaime/msime-engine/commit/d8109780b2efce3b3631976a1929d5466f432bfd))
* **shuangpin:** isolate ordered double-helpcode caches ([#194](https://github.com/metasequoiaime/msime-engine/issues/194)) ([3fc6d23](https://github.com/metasequoiaime/msime-engine/commit/3fc6d231e84ac7b8fde84251df7e6ecf968c812f))
* **shuangpin:** update canonical multi-syllable weights ([#192](https://github.com/metasequoiaime/msime-engine/issues/192)) ([0d2d6bc](https://github.com/metasequoiaime/msime-engine/commit/0d2d6bc433ce0d1cd1b343326c421694545e3cbf))
* **wubi:** allow mixed pinyin past prefix hints ([#198](https://github.com/metasequoiaime/msime-engine/issues/198)) ([3a6d749](https://github.com/metasequoiaime/msime-engine/commit/3a6d749cefbba806de99778c0c3665c44e11ee60))


### Performance Improvements

* **input:** skip unchanged autocorrect updates ([#186](https://github.com/metasequoiaime/msime-engine/issues/186)) ([b1d5d5e](https://github.com/metasequoiaime/msime-engine/commit/b1d5d5e09929eebad4970115e829a32dee046f3f))
* **local-modes:** reuse shipped dictionary connections ([#183](https://github.com/metasequoiaime/msime-engine/issues/183)) ([c0eb65d](https://github.com/metasequoiaime/msime-engine/commit/c0eb65ddb8a9b275d19bcf79992df4fd4125d6c1))

## [0.27.2](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.27.1...v0.27.2) (2026-09-20)


### Bug Fixes

* **nine-key:** stop synthesised candidates from outranking real words ([64f9c38](https://github.com/metasequoiaime/MSIME-Engine/commit/64f9c38086f0a7b1806d540f1ba0d84e7da6db4e))
* **nine-key:** 词典词条不再被合成候选压到下面 ([77bf1ae](https://github.com/metasequoiaime/MSIME-Engine/commit/77bf1aeafaaa3829a6aa514d52c424310d9012d3))

## [0.27.1](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.27.0...v0.27.1) (2026-09-20)


### Bug Fixes

* **dictionary:** drop segmentation fragments that reach the first candidate page ([bc46734](https://github.com/metasequoiaime/MSIME-Engine/commit/bc4673416c732dad2cb50429444ac3aaba6dc62a))
* **dictionary:** 删掉挤进首屏的分词碎片，并锁定 rime-ice 来源 ([67fd083](https://github.com/metasequoiaime/MSIME-Engine/commit/67fd083a4639a965e7a033cdbf53aa48c8c29af7))

## [0.27.0](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.26.1...v0.27.0) (2026-09-20)


### Features

* **quanpin:** arbitrate the two whole sentences, and repair the better one ([43add2c](https://github.com/metasequoiaime/MSIME-Engine/commit/43add2c81928ca2246b46b82653589a88063f1b8))
* **quanpin:** order the two whole sentences by score, not by source ([72bb5ca](https://github.com/metasequoiaime/MSIME-Engine/commit/72bb5caaf9785b583862f65b3599ae10c697d8e0))
* **quanpin:** repair the fallback sentence with the span the lattice reads better ([92529a6](https://github.com/metasequoiaime/MSIME-Engine/commit/92529a6d9b0ade6f864336e00d01af71d85d60b5))


### Bug Fixes

* **nine-key:** keep a zero-weight english word out of second place ([8371604](https://github.com/metasequoiaime/MSIME-Engine/commit/8371604d63d31341f3dc19ee50c2d33ff0784ae6))
* **nine-key:** keep a zero-weight english word out of second place ([538a0a7](https://github.com/metasequoiaime/MSIME-Engine/commit/538a0a79ea4098768a19410703fa85a15758b7f6))

## [0.26.1](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.26.0...v0.26.1) (2026-09-20)


### Performance Improvements

* **quanpin:** retune the lattice's phrase and context weights ([c8d1bd5](https://github.com/metasequoiaime/MSIME-Engine/commit/c8d1bd57a26a24b4166aef1158521e2fd9d4eb68))

## [0.26.0](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.25.0...v0.26.0) (2026-09-20)


### Features

* **dictionary:** pin zhwiki as the corpus the ngram stage counts ([ab368e5](https://github.com/metasequoiaime/MSIME-Engine/commit/ab368e563ce9c1c0632c7dd2f848c6bc5b009f90))
* **dictionary:** pin zhwiki as the ngram corpus, and ship the tables ([5edb8d0](https://github.com/metasequoiaime/MSIME-Engine/commit/5edb8d07e6c94edd76e4484ff9a61b999fe833a3))
* **dictionary:** ship the lattice context tables ([2ffe22d](https://github.com/metasequoiaime/MSIME-Engine/commit/2ffe22d06cee28f74656fad3c9c5a8d48a558753))


### Bug Fixes

* **ci:** allow the uncommitted ngram corpus path, and reflow the test comment ([ec7ebcf](https://github.com/metasequoiaime/MSIME-Engine/commit/ec7ebcfa91d06ae38eba496d286d0aa2ebbe3d5c))
* **tests:** derive the packaging fixture's product files from the assets contract ([1126175](https://github.com/metasequoiaime/MSIME-Engine/commit/112617584ee4ea20682aa6bd7a201712c97baa37))

## [0.25.0](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.24.0...v0.25.0) (2026-09-19)


### Features

* **dictionary:** build the lattice ngram tables as a stage ([#169](https://github.com/metasequoiaime/MSIME-Engine/issues/169)) ([e25f2b8](https://github.com/metasequoiaime/MSIME-Engine/commit/e25f2b81692317cfe7a939bd5d41746c19f059b0))

## [0.24.0](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.23.0...v0.24.0) (2026-09-19)


### Features

* **dictionary:** build custom/words.txt into the quanpin tables ([#166](https://github.com/metasequoiaime/MSIME-Engine/issues/166)) ([5eab393](https://github.com/metasequoiaime/MSIME-Engine/commit/5eab393735cf86ed5ef6014ee8ab1df02e31132f))
* **input:** expose pinyin segment boundaries ([#163](https://github.com/metasequoiaime/MSIME-Engine/issues/163)) ([5c03928](https://github.com/metasequoiaime/MSIME-Engine/commit/5c03928c89bcac729910683ed86e2c34628473bc))
* **input:** expose segment boundaries from session facade ([#165](https://github.com/metasequoiaime/MSIME-Engine/issues/165)) ([0531d42](https://github.com/metasequoiaime/MSIME-Engine/commit/0531d4211ab17d3ba43dc8ef86c05ec574b02fa8))


### Bug Fixes

* **runtime:** stage the lattice ngram tables into the dictionary generation ([#168](https://github.com/metasequoiaime/MSIME-Engine/issues/168)) ([d45268d](https://github.com/metasequoiaime/MSIME-Engine/commit/d45268d2c516f35c93bbe93477a653ec0a0ceaeb))

## [0.23.0](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.22.0...v0.23.0) (2026-09-18)


### Features

* **quanpin:** 让宿主可以要到全部整句读法，而不只是最好的那条 ([#160](https://github.com/metasequoiaime/MSIME-Engine/issues/160)) ([6213d0f](https://github.com/metasequoiaime/MSIME-Engine/commit/6213d0f14b3cf02e1ca264cbcc086f173a288d85))
* **session:** expose the sentence-alternatives request to hosts ([#162](https://github.com/metasequoiaime/MSIME-Engine/issues/162)) ([06e6700](https://github.com/metasequoiaime/MSIME-Engine/commit/06e6700bf51aef080f7610c8e335203725dddc87))

## [0.22.0](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.21.3...v0.22.0) (2026-09-17)


### Features

* **quanpin:** a sentence-accuracy eval set, and a corpus bigram for the lattice ([2dc1a9d](https://github.com/metasequoiaime/MSIME-Engine/commit/2dc1a9d57e119ec5e7b6925bcd05cbf3defbb4d5))
* **quanpin:** search six sentences, show one, and open the seam to rescore them ([575d568](https://github.com/metasequoiaime/MSIME-Engine/commit/575d568c5d35401bdd3f77b51d7c7f2819b2172d))


### Performance Improvements

* **quanpin:** map the ngram tables instead of reading them ([5d99f3d](https://github.com/metasequoiaime/MSIME-Engine/commit/5d99f3d7b1e7833d56e4fd98dbdf1a2997d17cb2))

## [0.21.3](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.21.2...v0.21.3) (2026-09-17)


### Bug Fixes

* stop assembled sentences from crowding out dictionary candidates ([9710c3b](https://github.com/metasequoiaime/MSIME-Engine/commit/9710c3ba630a4baa4d2877eab1e14532670da10a))

## [0.21.2](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.21.1...v0.21.2) (2026-09-17)


### Bug Fixes

* **quanpin:** rank whole sentences behind exact dictionary hits ([375c067](https://github.com/metasequoiaime/MSIME-Engine/commit/375c0670e582d7b829b2f8870432aea455d7adf9))
* **quanpin:** rank whole sentences behind exact dictionary hits ([a40b845](https://github.com/metasequoiaime/MSIME-Engine/commit/a40b8452593af5fa6e931ef75315618c0118a1db))

## [0.21.1](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.21.0...v0.21.1) (2026-09-15)


### Bug Fixes

* rank nine-key spellings by digit length ([6f235a5](https://github.com/metasequoiaime/MSIME-Engine/commit/6f235a5144e02bf0468ab7ee2d625cd2181df931))
* rank nine-key spellings by digit length ([acfc34a](https://github.com/metasequoiaime/MSIME-Engine/commit/acfc34ac38460267062cbe90296df952e55944a4))

## [0.21.0](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.20.0...v0.21.0) (2026-09-15)


### Features

* **nine-key:** spell English words on the grid ([b775b81](https://github.com/metasequoiaime/MSIME-Engine/commit/b775b81bf0547b113d29fbece2dc6a85cec47073))


### Bug Fixes

* **nine-key:** keep the English mode in the grid's snapshot ([7ea5350](https://github.com/metasequoiaime/MSIME-Engine/commit/7ea5350ba0990862875302c54b5c4d0caf9bb80a))

## [0.20.0](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.19.0...v0.20.0) (2026-09-14)


### Features

* **ipc:** define negotiated keyboard composition cancellation ([#144](https://github.com/metasequoiaime/MSIME-Engine/issues/144)) ([aa0dfcb](https://github.com/metasequoiaime/MSIME-Engine/commit/aa0dfcb930f1b21f9bcaa7bf785dca2f0af1f198))
* **voice:** select capture devices by backend endpoint identity ([#142](https://github.com/metasequoiaime/MSIME-Engine/issues/142)) ([f0d212c](https://github.com/metasequoiaime/MSIME-Engine/commit/f0d212c8a90dac2f70d48a5a149e4f71080b4e7b))


### Bug Fixes

* **engine:** preserve fuzzy candidate ranking scales ([886fe2f](https://github.com/metasequoiaime/MSIME-Engine/commit/886fe2f0ca015e1d36d046a607682cc7d2fb9a2c))
* **japanese:** complete minus-key long-vowel input ([#147](https://github.com/metasequoiaime/MSIME-Engine/issues/147)) ([f331a45](https://github.com/metasequoiaime/MSIME-Engine/commit/f331a45a38903bbbe92d3c9da7449c6baa0c5adf))
* **japanese:** resolve n before long vowel mark ([#146](https://github.com/metasequoiaime/MSIME-Engine/issues/146)) ([a0c60be](https://github.com/metasequoiaime/MSIME-Engine/commit/a0c60bef0c31f6434940563ade9e566a700885ed))

## [0.19.0](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.18.0...v0.19.0) (2026-09-14)


### Features

* **contracts:** separate voice controller v2 from TSF identity ([#139](https://github.com/metasequoiaime/MSIME-Engine/issues/139)) ([d6a1f6b](https://github.com/metasequoiaime/MSIME-Engine/commit/d6a1f6b62b8498cc2d9252aa312147cd06de8b80))
* **english:** cache fetched glosses in a durable user file ([#141](https://github.com/metasequoiaime/MSIME-Engine/issues/141)) ([f19e1f3](https://github.com/metasequoiaime/MSIME-Engine/commit/f19e1f315f5d3933786165081ccc36af827146e2))

## [0.18.0](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.17.0...v0.18.0) (2026-09-14)


### Features

* **contracts:** define voice control hello ([6215a99](https://github.com/metasequoiaime/MSIME-Engine/commit/6215a99425ff417c1b3552d4842801db5720a3ea))
* **contracts:** define voice control message ([c54b1fa](https://github.com/metasequoiaime/MSIME-Engine/commit/c54b1fad92c463adf3c83646c9a9d6e29730f176))
* **voice:** add bounded shared Doubao wire codec ([0850d01](https://github.com/metasequoiaime/MSIME-Engine/commit/0850d018948fa90d2caebb4786393a105500ead2))


### Bug Fixes

* **punctuation:** balance auto-closed book titles ([c5be6a2](https://github.com/metasequoiaime/MSIME-Engine/commit/c5be6a29ea8caef5a5bfd4eba441aa374c5be630))
* **punctuation:** balance auto-closed book titles ([c1de18a](https://github.com/metasequoiaime/MSIME-Engine/commit/c1de18a97a5bf2ed9a3f6afc507977a9bdc8b796))

## [0.17.0](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.16.0...v0.17.0) (2026-09-14)


### Features

* **engine:** support ordered batches of online candidates ([#128](https://github.com/metasequoiaime/MSIME-Engine/issues/128)) ([74be030](https://github.com/metasequoiaime/MSIME-Engine/commit/74be030d1ccbe49cc18aec59b92f77118e580229))
* **handwriting:** share Chinese-first candidate ordering ([#130](https://github.com/metasequoiaime/MSIME-Engine/issues/130)) ([df45b44](https://github.com/metasequoiaime/MSIME-Engine/commit/df45b44c9153f4f273a7cb1e49c44784a9f0f4a0))
* **japanese:** commit the reading as typed ([233a267](https://github.com/metasequoiaime/MSIME-Engine/commit/233a2671903efca5fb17e6ff55476a108c111d0d))
* **japanese:** let the long vowel mark into the composition ([7945d70](https://github.com/metasequoiaime/MSIME-Engine/commit/7945d70a09caf78d78be87efc835379b03f0f3e9))
* **japanese:** long vowel, kana variants, and committing the reading ([3ec562d](https://github.com/metasequoiaime/MSIME-Engine/commit/3ec562d7de7e329083e46c52286d2e703979f1d3))
* **japanese:** modify the kana just typed instead of inserting a new one ([0de3ce7](https://github.com/metasequoiaime/MSIME-Engine/commit/0de3ce780027da2bcd8f762c5ce9d7a28c780fd5))
* **nine-key:** offer English words for the digits typed ([#126](https://github.com/metasequoiaime/MSIME-Engine/issues/126)) ([15ff08f](https://github.com/metasequoiaime/MSIME-Engine/commit/15ff08fc50ff9b469dae0f4bdabeae76c2a66b91))
* **quanpin:** offer the phrases that continue a finished spelling ([942e069](https://github.com/metasequoiaime/MSIME-Engine/commit/942e06995f2e35d1a8e2fb5fbc8e4691c6622cfd))
* **quanpin:** offer the phrases that continue a finished spelling ([636434a](https://github.com/metasequoiaime/MSIME-Engine/commit/636434a1caa392a25bf9db6e5f80b6dfac0d7956))


### Bug Fixes

* audit findings across the engine, with regression tests ([be394c7](https://github.com/metasequoiaime/MSIME-Engine/commit/be394c74a0336a8e7214f60063eec55dc950744d))
* **core:** reset the wubi fallback flag and requery on a helpcode toggle ([4191aad](https://github.com/metasequoiaime/MSIME-Engine/commit/4191aad0391db1f4cb4587369ab00376b4f63bf9))
* **engine:** keep online batch method in session namespace ([b9fb725](https://github.com/metasequoiaime/MSIME-Engine/commit/b9fb725a9b5bf4dc9669f47460b1e5e36a35d7f3))
* **engine:** keep online batch method in session namespace ([b821f65](https://github.com/metasequoiaime/MSIME-Engine/commit/b821f65b076aa404406f56bad77809cf4a2965b0))
* **japanese:** correct nn and tch romaji, and stop dropping prefix lemmas ([f2cd24f](https://github.com/metasequoiaime/MSIME-Engine/commit/f2cd24f97132546995daf7798eb6aea90650937d))
* **local-modes:** drop the blank date candidate when the lunar table has no entry ([914d452](https://github.com/metasequoiaime/MSIME-Engine/commit/914d452e506f810e20b199f88ca71018dcd26e99))
* **quanpin:** accept every valid spelling and stop dropping merged sentences ([2752f1b](https://github.com/metasequoiaime/MSIME-Engine/commit/2752f1b2c88dc79a6926657a6372c2af29baa592))
* **shuangpin:** honour a manual delimiter before a trailing helpcode letter ([e975002](https://github.com/metasequoiaime/MSIME-Engine/commit/e975002a6232e29f6634d72436bd7219cc8872a2))
* **user-dictionary:** respect BEGIN IMMEDIATE and clear the rank-0 counter ([286729a](https://github.com/metasequoiaime/MSIME-Engine/commit/286729a4222cec9a4fe66fa1175bc7032f35004c))


### Performance Improvements

* **engine:** stop copying candidate vectors on cache probes and char counting ([53a1f9f](https://github.com/metasequoiaime/MSIME-Engine/commit/53a1f9f9e173445f8809ee19160db2fc6dd236f6))

## [0.16.0](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.15.0...v0.16.0) (2026-09-12)


### Features

* **engine:** expose shuangpin raw preedit option ([#120](https://github.com/metasequoiaime/MSIME-Engine/issues/120)) ([429e886](https://github.com/metasequoiaime/MSIME-Engine/commit/429e886bdca5c44093971ee735b8db050b3671cd))


### Bug Fixes

* **input:** learn generated sentence candidates ([#123](https://github.com/metasequoiaime/MSIME-Engine/issues/123)) ([1e8e557](https://github.com/metasequoiaime/MSIME-Engine/commit/1e8e55726baf381b1d699cf626850acec5ee4b3e))
* **user-dictionary:** keep cross-key promotion above the rows it cannot demote ([25ccf6e](https://github.com/metasequoiaime/MSIME-Engine/commit/25ccf6e1f6a541b8a345cb8aeea211b31b2a6f4f))

## [0.15.0](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.14.0...v0.15.0) (2026-09-12)


### Features

* **handwriting:** add offline stroke recognition and packaged Chinese model ([9af88ea](https://github.com/metasequoiaime/MSIME-Engine/commit/9af88ea6141b430fc30c25821492e42d00f8f8b0))
* **handwriting:** add offline stroke recognition engine ([1df2165](https://github.com/metasequoiaime/MSIME-Engine/commit/1df2165a1029163cef7766df0d942b9e13bf0b7a))
* **handwriting:** expose reusable engine target ([5b54413](https://github.com/metasequoiaime/MSIME-Engine/commit/5b544136e39f8b7d523642728b874ab9e3b3bb25))
* **session:** expose candidate display annotations ([7290620](https://github.com/metasequoiaime/MSIME-Engine/commit/7290620dd79e98c466f8d8eaf2236c0a66a9188b))
* **session:** expose candidate display annotations ([0a03ba2](https://github.com/metasequoiaime/MSIME-Engine/commit/0a03ba2a3ff9515ee44b6b674260dd63933c98b2))


### Bug Fixes

* **handwriting:** link zinnia statically on Windows ([#117](https://github.com/metasequoiaime/MSIME-Engine/issues/117)) ([4f80900](https://github.com/metasequoiaime/MSIME-Engine/commit/4f80900fc94bbc6609b45419c5f59aeb62786bc4))
* **input:** preserve fallback sentence readings ([e1f46d0](https://github.com/metasequoiaime/MSIME-Engine/commit/e1f46d024fdef0e78f2ad5dafb3a147ad90b4570))
* **session:** expose cache reset API ([659084c](https://github.com/metasequoiaime/MSIME-Engine/commit/659084c19f339c7a519de18dede46e01f4acad99))
* **session:** expose cache reset API ([f00188e](https://github.com/metasequoiaime/MSIME-Engine/commit/f00188e138713eb1d028e73476b0d2bebd69043e))

## [0.14.0](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.13.1...v0.14.0) (2026-09-11)


### Features

* **session:** expose candidate sources in snapshots ([70106cc](https://github.com/metasequoiaime/MSIME-Engine/commit/70106ccd8e7d823acf863e9cd3fcd8fa69678b95))
* **session:** expose candidate sources in snapshots ([2638835](https://github.com/metasequoiaime/MSIME-Engine/commit/26388356cdd8a107c1a6f6052f7d1aa9a53ccc89))

## [0.13.1](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.13.0...v0.13.1) (2026-09-11)


### Bug Fixes

* **session:** assign snapshot profile by field ([9727eaa](https://github.com/metasequoiaime/MSIME-Engine/commit/9727eaaa1c85837f37e1dad1e424c4fc5c333d26))
* **session:** assign snapshot profile to its named field ([b06aadf](https://github.com/metasequoiaime/MSIME-Engine/commit/b06aadfe43de2a341d83781bfa29ffc4017d97cb))

## [0.13.0](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.12.1...v0.13.0) (2026-09-11)


### Features

* **session:** expose shuangpin profile in snapshot ([d8448a3](https://github.com/metasequoiaime/MSIME-Engine/commit/d8448a314667ace97b0cb6487fdc9fa90f10e5ee))
* **session:** expose shuangpin profile in snapshot ([2cc2c56](https://github.com/metasequoiaime/MSIME-Engine/commit/2cc2c5620343e429da5e72e16ddceac609b2915c))

## [0.12.1](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.12.0...v0.12.1) (2026-09-11)


### Bug Fixes

* **api:** expose personal dictionary declarations ([a10925e](https://github.com/metasequoiaime/MSIME-Engine/commit/a10925e9f8ffd700ce13c2e35b5668e8b2d53012))
* **api:** expose personal dictionary declarations ([d5cbd33](https://github.com/metasequoiaime/MSIME-Engine/commit/d5cbd3391764469bc3ac85fe5d461e1c974fd797))

## [0.12.0](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.11.0...v0.12.0) (2026-09-11)


### Features

* **punctuation:** add paired punctuation session toggle ([0013a9c](https://github.com/metasequoiaime/MSIME-Engine/commit/0013a9c69d51b14a8150eb5e2218015bc7fb1b23))
* **punctuation:** add paired punctuation toggle ([f9b5039](https://github.com/metasequoiaime/MSIME-Engine/commit/f9b5039cd5a9f11b0340b71598fc5729a4e20199))
* **punctuation:** add session punctuation options ([a9ca0d1](https://github.com/metasequoiaime/MSIME-Engine/commit/a9ca0d190de60a34e73601d98833619e3fd3489e))
* **punctuation:** add session punctuation options ([69b1a5c](https://github.com/metasequoiaime/MSIME-Engine/commit/69b1a5cf266bc48b746e6cfc4f8af317b43fd986))


### Bug Fixes

* **wubi:** answer an unfinished code with the codes it can still become ([ee232b4](https://github.com/metasequoiaime/MSIME-Engine/commit/ee232b419634826f8ef2b266169e7c9ea26b9e09))
* **wubi:** answer an unfinished code with the codes it can still become ([054ed5c](https://github.com/metasequoiaime/MSIME-Engine/commit/054ed5c69bc72b2847920180b5e088dc873ae445))

## [0.11.0](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.10.0...v0.11.0) (2026-09-11)


### Features

* **punctuation:** add session punctuation lock ([3c72e73](https://github.com/metasequoiaime/MSIME-Engine/commit/3c72e739d61b7b52050405ce54b8ed850bb23c3d))
* **punctuation:** add session punctuation lock ([24953b7](https://github.com/metasequoiaime/MSIME-Engine/commit/24953b73531f6877066f35f4a4d24e112d3e4178))


### Bug Fixes

* **contracts:** space out product-lock download attempts ([3b2d52b](https://github.com/metasequoiaime/MSIME-Engine/commit/3b2d52b8961804962d622af0477da32d1f8d5682))
* **contracts:** space out product-lock download attempts ([36964db](https://github.com/metasequoiaime/MSIME-Engine/commit/36964db71e0ebfb2bc336dc2ce94d09fdc867e4c))

## [0.10.0](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.9.0...v0.10.0) (2026-09-10)


### Features

* **engine:** 全拼纠错分类开关、简拼守卫与 preedit 原始输入显示 ([e66634c](https://github.com/metasequoiaime/MSIME-Engine/commit/e66634c48eb39695b0f6be9824a771e7ed6a2f1b))
* **engine:** 全拼纠错分类开关、简拼守卫与 preedit 原始输入显示 ([c508c84](https://github.com/metasequoiaime/MSIME-Engine/commit/c508c845eeea9d703b0aaccbd64c0f7c1e712010))

## [0.9.0](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.8.0...v0.9.0) (2026-09-09)


### Features

* **engine:** report when pinyin answered a wubi code ([f3d37d6](https://github.com/metasequoiaime/MSIME-Engine/commit/f3d37d62b63d1602da3b79532e7a7d697e8ec90a))
* **engine:** 在快照中标出由拼音回退作答的候选 ([54abc77](https://github.com/metasequoiaime/MSIME-Engine/commit/54abc77c4971a344148951f29f14eaa37566a13f))

## [0.8.0](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.7.1...v0.8.0) (2026-09-09)


### Features

* **engine:** answer an unmatched wubi code with pinyin ([30e2681](https://github.com/metasequoiaime/MSIME-Engine/commit/30e2681e15ce291fdab513a3f7f67f29e11c0a9a))
* **engine:** 五笔支持混拼 ([84008ac](https://github.com/metasequoiaime/MSIME-Engine/commit/84008ac23f4380d4f1fa0740feb5b4357ff8f887))
* **session:** expose live Chinese punctuation mode ([e637e3d](https://github.com/metasequoiaime/MSIME-Engine/commit/e637e3db59669caf29e257f0c21bfd6c35413a03))
* **session:** expose live Chinese punctuation mode ([a9f5c65](https://github.com/metasequoiaime/MSIME-Engine/commit/a9f5c65ed4f8f6df933ddf24b82ce670309e78e4))


### Bug Fixes

* **engine:** 修正混拼评审发现的问题 ([61153e3](https://github.com/metasequoiaime/MSIME-Engine/commit/61153e37e93467e469c96577f74e64856d7e7892))

## [0.7.1](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.7.0...v0.7.1) (2026-09-08)


### Performance Improvements

* **user-dictionary:** keep the default journal connection open ([da61ca4](https://github.com/metasequoiaime/MSIME-Engine/commit/da61ca472eda23445ae0ecd2e4d43868063cb36a))
* **user-dictionary:** keep the default journal connection open ([ee20339](https://github.com/metasequoiaime/MSIME-Engine/commit/ee2033924d2bca4daaeef1155d1870c3fdfef1de))

## [0.7.0](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.6.0...v0.7.0) (2026-09-08)


### Features

* **dictionary:** 支持完整用户词库状态传输与代际准备 ([#79](https://github.com/metasequoiaime/MSIME-Engine/issues/79)) ([dd58732](https://github.com/metasequoiaime/MSIME-Engine/commit/dd587326da8f5ce5a1b98dc8135dc1787c5dd90a))
* **engine:** support configurable fuzzy pinyin per session ([#81](https://github.com/metasequoiaime/MSIME-Engine/issues/81)) ([5d9a031](https://github.com/metasequoiaime/MSIME-Engine/commit/5d9a031fa61ccf70b7cc9ee27340bdd039b254b5))


### Bug Fixes

* **pinyin:** 修复 ong 韵尾漏写 g 的完整词组候选 ([#77](https://github.com/metasequoiaime/MSIME-Engine/issues/77)) ([b377a18](https://github.com/metasequoiaime/MSIME-Engine/commit/b377a1892d89e79cd2c2bda308cbcfd7e6ddf62f))

## [0.6.0](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.5.0...v0.6.0) (2026-09-08)


### Features

* **contracts:** 定义输入法共通后端 API v1 契约 ([#74](https://github.com/metasequoiaime/MSIME-Engine/issues/74)) ([d59c202](https://github.com/metasequoiaime/MSIME-Engine/commit/d59c2028c64a314d0b14753422d56fc1117b91cd))
* **dictionary:** expose transactional personal entry editing ([#72](https://github.com/metasequoiaime/MSIME-Engine/issues/72)) ([ec1659c](https://github.com/metasequoiaime/MSIME-Engine/commit/ec1659caf0b9b37706419929c691cdecde575e5a))

## [0.5.0](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.4.1...v0.5.0) (2026-09-07)


### Features

* **nine-key:** support learning and candidate management ([#70](https://github.com/metasequoiaime/MSIME-Engine/issues/70)) ([f00ab4e](https://github.com/metasequoiaime/MSIME-Engine/commit/f00ab4ea1e701c2e8fb2ba94a3ecc796aacdc2f8))

## [0.4.1](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.4.0...v0.4.1) (2026-09-07)


### Bug Fixes

* **japanese:** map immutable sentence model on POSIX ([e40ea08](https://github.com/metasequoiaime/MSIME-Engine/commit/e40ea087f558ba97b84cafa2ce2edc08f37f8bc6))
* **japanese:** reduce sentence-model heap memory ([d7fd49b](https://github.com/metasequoiaime/MSIME-Engine/commit/d7fd49bb71fe5b303a9081415c30fdb2940de997))

## [0.4.0](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.3.0...v0.4.0) (2026-09-07)


### Features

* **input:** add nine-key pinyin composition ([1f98a2b](https://github.com/metasequoiaime/MSIME-Engine/commit/1f98a2b10b8e072217a805e86d5c771e09269302))
* **input:** add nine-key pinyin composition to public sessions ([e817376](https://github.com/metasequoiaime/MSIME-Engine/commit/e817376414e2e91cbc76266226d5668711c843bf))


### Bug Fixes

* **input:** prioritize the candidate reading in nine-key spelling choices ([29383dd](https://github.com/metasequoiaime/MSIME-Engine/commit/29383dd841ab663c0b34958f6b3abfa68ffded97))
* **input:** retain locked nine-key syllables after partial selection ([7b614c1](https://github.com/metasequoiaime/MSIME-Engine/commit/7b614c16fa969a3a9ed29aaa594f9f5f2beab7e0))

## [0.3.0](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.2.0...v0.3.0) (2026-09-06)


### Features

* **contracts:** centralize punctuation and product lock contracts ([9bec871](https://github.com/metasequoiaime/MSIME-Engine/commit/9bec871eb6c84224c93586be23604df64bb6258c))
* **contracts:** centralize punctuation and product lock contracts ([7d8ec04](https://github.com/metasequoiaime/MSIME-Engine/commit/7d8ec04055c52c6771c127eaf290ac3ce7285c8d))
* Limit local sentence suggestions to the Google Pinyin result and the ([f139737](https://github.com/metasequoiaime/MSIME-Engine/commit/f1397379bbf630bdf26863bbffbfd9d7a9b9221b))
* 优化长句候选结果条数并减少重复候选 ([c1dea47](https://github.com/metasequoiaime/MSIME-Engine/commit/c1dea47cbe62b5c2b95a869d148c351d21597ba4))


### Bug Fixes

* **ci:** allow Windows filesystem tests more time ([c6130cf](https://github.com/metasequoiaime/MSIME-Engine/commit/c6130cf63dcf96f1780642e7d6ee91a26c6e5fad))
* **ci:** allow Windows filesystem tests more time ([88191c0](https://github.com/metasequoiaime/MSIME-Engine/commit/88191c03aefe4ec2ee8037d1fba931829d1fd8d5))
* **ci:** avoid duplicate CodeQL scans on pushes ([2983fe8](https://github.com/metasequoiaime/MSIME-Engine/commit/2983fe87b5a715eb86500d6337d46894d9aa16f8))
* **ci:** avoid duplicate CodeQL scans on pushes ([ee4d3b3](https://github.com/metasequoiaime/MSIME-Engine/commit/ee4d3b324dc2b8a8710295a329e25b5a98c8685f))
* **release:** release-please 显式指向 main ([537b7b0](https://github.com/metasequoiaime/MSIME-Engine/commit/537b7b075b679163554c89c5bc1e140e9b48f122))
* **release:** target main for release-please and back-merge main ([be4eeb4](https://github.com/metasequoiaime/MSIME-Engine/commit/be4eeb418b1306ca5e44607c890cef45db278b6f))

## [0.2.0](https://github.com/metasequoiaime/MSIME-Engine/compare/v0.1.0...v0.2.0) (2026-09-06)


### Features

* add configurable ranking and fixed candidate positions ([9dff65e](https://github.com/metasequoiaime/MSIME-Engine/commit/9dff65e1e8b27df5f79dda3e139761aa5322818c))
* add database-backed initial candidate expansion ([ed7e5e2](https://github.com/metasequoiaime/MSIME-Engine/commit/ed7e5e2819176da97a8107026d2dd5d170ad89a4))
* add persistent user dictionary operation journal ([40cc6f8](https://github.com/metasequoiaime/MSIME-Engine/commit/40cc6f8ce9828913b560d0e7778f0e0cd49b910d))
* **assets:** 补齐公共运行时资源打包与发布 ([9d121bf](https://github.com/metasequoiaime/MSIME-Engine/commit/9d121bf00918fa04f57640b5a0af79e36be7771f))
* **build:** add a reproducible pipeline for every shipping dictionary ([24df653](https://github.com/metasequoiaime/MSIME-Engine/commit/24df6533a33e54993491926d490a5f51ad6522fb))
* **build:** add a reproducible pipeline for every shipping dictionary ([c0662c7](https://github.com/metasequoiaime/MSIME-Engine/commit/c0662c73ca0637be72e1b0a9259f212328f4447c))
* **build:** ship the Mozc licence notice with the Japanese model ([cfeff1e](https://github.com/metasequoiaime/MSIME-Engine/commit/cfeff1e356ec918ab4da3771b296d4a69ab2251c))
* **contracts:** generate shared WebView messages and validate both runtimes ([9a4b0c6](https://github.com/metasequoiaime/MSIME-Engine/commit/9a4b0c62fd89d27698cf27d2b43aa4b22727dc67))
* **contracts:** share Windows wire definitions and negotiate main sessions ([127019f](https://github.com/metasequoiaime/MSIME-Engine/commit/127019f252cab9146f7a4e184f77cd6ed13fcf56))
* **contracts:** validate dictionary product profiles and selected payloads ([1a3b259](https://github.com/metasequoiaime/MSIME-Engine/commit/1a3b2598ce64dc891a0ac2196a39d93ef3e20f00))
* **core:** add Linux desktop input features ([#9](https://github.com/metasequoiaime/MSIME-Engine/issues/9)) ([8ef4922](https://github.com/metasequoiaime/MSIME-Engine/commit/8ef4922ce93ecff10b4183078612934daf9d9820))
* **core:** expose safe online candidate updates ([b234ea1](https://github.com/metasequoiaime/MSIME-Engine/commit/b234ea1e006c021b36747184ee0c66a9fbd48f00))
* **core:** expose safe online candidate updates ([9a7062f](https://github.com/metasequoiaime/MSIME-Engine/commit/9a7062f2dccc31a374d12d86dc5c5899882a3f1c))
* **data:** expose and verify shared desktop and mobile dictionary products ([73e92ed](https://github.com/metasequoiaime/MSIME-Engine/commit/73e92edcab2cb2e7d8296948a9e0f1e21903d206))
* **data:** publish desktop and mobile build profiles with provenance ([78c6fce](https://github.com/metasequoiaime/MSIME-Engine/commit/78c6fced19ec630d20be191a7f00013e0b640c48))
* **dict:** add optional Unreal Engine and Houdini terminology pack ([bcf83a6](https://github.com/metasequoiaime/MSIME-Engine/commit/bcf83a65105f068e94be921b75ce40507eb12bb2))
* **dict:** add optional Unreal Engine and Houdini terminology pack ([91f472e](https://github.com/metasequoiaime/MSIME-Engine/commit/91f472e0a6d5772d5728400c90aef2b1d70fa0f7))
* **dictionary:** filter single-character entries by whitelist ([94f724a](https://github.com/metasequoiaime/MSIME-Engine/commit/94f724adbb466cdb10ed8559ffa5a62357f474a7))
* **dictionary:** 产物清单记录本次构建的许可状态 ([806a620](https://github.com/metasequoiaime/MSIME-Engine/commit/806a620aeaffaedc9e60216f98d681d111fd4b96))
* **dictionary:** 增加用户词库迁移转换器 ([d42b78f](https://github.com/metasequoiaime/MSIME-Engine/commit/d42b78fbfdf39e967ae03830e13cdb6f2700cefe))
* **dictionary:** 默认只构建有再分发授权的数据 ([1d5f6a9](https://github.com/metasequoiaime/MSIME-Engine/commit/1d5f6a9cd16eca597b0cd99045a7a5965c8559ac))
* **engine:** add journaled candidate removal to public Session ([#41](https://github.com/metasequoiaime/MSIME-Engine/issues/41)) ([e1216bb](https://github.com/metasequoiaime/MSIME-Engine/commit/e1216bb63b610ac2738d0f1f8b6ade2044cd5464))
* **engine:** expose candidate pinning through public Session ([#40](https://github.com/metasequoiaime/MSIME-Engine/issues/40)) ([9f8df0e](https://github.com/metasequoiaime/MSIME-Engine/commit/9f8df0eeb938402827da363cf57ebe5abce8b172))
* **engine:** expose desktop composition intents through Session ([#37](https://github.com/metasequoiaime/MSIME-Engine/issues/37)) ([020e906](https://github.com/metasequoiaime/MSIME-Engine/commit/020e906abe5a6af78cec70a1dced8789812eac52))
* **pinyin:** preserve canonical pronunciations for phrase creation ([22ced21](https://github.com/metasequoiaime/MSIME-Engine/commit/22ced21d308cf22f7dc88b810f1b0c3c1087f602))
* **punctuation:** convert the remaining keys and nest book title marks ([93358b5](https://github.com/metasequoiaime/MSIME-Engine/commit/93358b5d3b69ab25548222da574442a4594b87dd))
* **quanpin:** add generic pinyin typo correction ([ecb28f6](https://github.com/metasequoiaime/MSIME-Engine/commit/ecb28f62446c8e1a2c8f8a88f73d9f2492cf5277))
* **quanpin:** add generic pinyin typo correction ([84a48e1](https://github.com/metasequoiaime/MSIME-Engine/commit/84a48e1417b581eb49087b219aef770995373c2a))
* **quanpin:** add typing autocorrection for transposed and neighbor-key misspellings ([dfa69d5](https://github.com/metasequoiaime/MSIME-Engine/commit/dfa69d5131c9f6ccf817e7798174df4aab1cf146))
* **quanpin:** compose sentences with a dictionary word lattice ([243ceef](https://github.com/metasequoiaime/MSIME-Engine/commit/243ceefbee476f0f504fc784a4da399fc610591f))
* **quanpin:** dictionary word lattice for sentence composition ([026c580](https://github.com/metasequoiaime/MSIME-Engine/commit/026c580f30a8d866ce395b627c315333917a1d49))
* **quanpin:** extend pinyin typo aliases ([1017239](https://github.com/metasequoiaime/MSIME-Engine/commit/1017239127da85bad108d7d7e9d72a8a9ef9f668))
* **quanpin:** support bounded alternative syllable segmentations. ([911f7e1](https://github.com/metasequoiaime/MSIME-Engine/commit/911f7e1e28ab8fdff8907630a020d8e1b4733f95))
* **quanpin:** typing autocorrect for transposed and neighbor-key misspellings ([18bf54f](https://github.com/metasequoiaime/MSIME-Engine/commit/18bf54fc2265a6e881b8ac7b1c67b26d3bdeec58))
* **release:** 接入 release-please，并让引擎改动在下游前端上验证 ([d29cde6](https://github.com/metasequoiaime/MSIME-Engine/commit/d29cde6b075e631ba788848a898628fa9334d7fc))
* **session:** expose context-specific fixed candidate positions ([1fb9478](https://github.com/metasequoiaime/MSIME-Engine/commit/1fb9478721206be9e8f67b2969614ddf45df3834))
* **session:** expose context-specific fixed candidate positions ([dbffd94](https://github.com/metasequoiaime/MSIME-Engine/commit/dbffd943269bc0dd8250c76afdf372a62acc7120))
* **session:** own composition caret and middle editing intents ([#45](https://github.com/metasequoiaime/MSIME-Engine/issues/45)) ([8eb4b4e](https://github.com/metasequoiaime/MSIME-Engine/commit/8eb4b4e1ea448aa552eda773fd43d40daba34394))
* **user-dictionary:** track inserted entries and bound ranking weights ([9d4fd93](https://github.com/metasequoiaime/MSIME-Engine/commit/9d4fd93c8caac168b784fb165ef549389e0e77dd))
* **voice:** allow explicit bounded WAV upload durations ([ebef188](https://github.com/metasequoiaime/MSIME-Engine/commit/ebef188b7d1fd8a317be3c32347dc078c3ee2ac7))
* **voice:** expose shared speech libraries and a macOS host ([5eacf57](https://github.com/metasequoiaime/MSIME-Engine/commit/5eacf574ac5fb4c2eb91247095c3befe27b1cecc))
* **voice:** share provider request and response codecs ([cc21e42](https://github.com/metasequoiaime/MSIME-Engine/commit/cc21e42916cf93ce43051fec474e87eeeba305bf))
* **voice:** share transcription and polish protocol codecs ([04e754a](https://github.com/metasequoiaime/MSIME-Engine/commit/04e754acaf5bdbd9470cd79dcac757316d95aee1))


### Bug Fixes

* **api:** 保持在线请求聚合初始化兼容 ([c868101](https://github.com/metasequoiaime/MSIME-Engine/commit/c868101fdd8bbf8f90b17c0d3b0ba4031194bf36))
* **build:** ship the Mozc licence notice and fix the push trigger ([0c7368c](https://github.com/metasequoiaime/MSIME-Engine/commit/0c7368cc46020b4f6c8ac7f9bf62b68ff0b303cd))
* **ci:** trigger the dictionary build on main, not master ([43a4e77](https://github.com/metasequoiaime/MSIME-Engine/commit/43a4e77ba3da11394c2e9e728ef948282269d3c4))
* **ci:** 下游 job 排除依赖运行环境的测试 ([51e3641](https://github.com/metasequoiaime/MSIME-Engine/commit/51e3641ceb1abadd933624767ef2b2a2c5012699))
* **ci:** 格式化时排除生成的头文件 ([b8abd77](https://github.com/metasequoiaime/MSIME-Engine/commit/b8abd77632ac11360ebca075926024cf6261eebc))
* **contracts:** add bounded settings dictionary pagination ([d0dc0c2](https://github.com/metasequoiaime/MSIME-Engine/commit/d0dc0c2b594b5540b5de99ad12085c786410626e))
* **contracts:** bound settings dictionary queries with pagination ([403b4a4](https://github.com/metasequoiaime/MSIME-Engine/commit/403b4a4d72b0ab5110155aeb4a1f0c170730fc7b))
* **decoder:** 更新已合并的模型重载内存修复 ([e31d726](https://github.com/metasequoiaime/MSIME-Engine/commit/e31d726ed9828f5e1e05fa60c3b36c9ee3769aa2))
* **decoder:** 更新通过泄漏检测的词典资源释放修复 ([2c8fdea](https://github.com/metasequoiaime/MSIME-Engine/commit/2c8fdea6c72aad619e2bedcd7d445052eb1648f1))
* dedupe AI/cloud inserts in quanpin and shuangpin series caches ([b028f8c](https://github.com/metasequoiaime/MSIME-Engine/commit/b028f8c6fc84f899a815b6c8bb046af826faa2eb))
* **dictionary:** 许可开关改用「显式为真」的白名单 ([5e22539](https://github.com/metasequoiaime/MSIME-Engine/commit/5e22539c344eb471cad2e7b15299dd8bd8445ba6))
* **dictionary:** 词库校验的行数下限随许可开关切换 ([bd4a903](https://github.com/metasequoiaime/MSIME-Engine/commit/bd4a90358af24065957cd54995cea226d9e940cd))
* **mix:** 快捷短语示例改用占位数据 ([9c78468](https://github.com/metasequoiaime/MSIME-Engine/commit/9c784683c2da95858d2c71da34a3e89e26f2b297))
* **mix:** 快捷短语示例改用占位数据 ([6128e60](https://github.com/metasequoiaime/MSIME-Engine/commit/6128e609b21304f22b68eeac08dd8c2a5ca15a74))
* pin through a git registry, not builtin-baseline ([1e294fe](https://github.com/metasequoiaime/MSIME-Engine/commit/1e294fe638f79e92528323cdb4f4c35e0c0f6fab))
* **pinyin:** deduplicate consecutive manual separators ([c98e4bb](https://github.com/metasequoiaime/MSIME-Engine/commit/c98e4bbc01ddabd31a3ea2f77022557b0543ec6f))
* **pinyin:** expand initial candidates in segmented queries ([b797b23](https://github.com/metasequoiaime/MSIME-Engine/commit/b797b23e6faa4a439c196d6bb58bd8c959eba5f4))
* **product:** freeze standalone SQLite files and test real Engine consumers ([74e0deb](https://github.com/metasequoiaime/MSIME-Engine/commit/74e0debb3f718d0ac8aa826d513cd162b67fc08c))
* **quanpin:** enumerate alternative segmentations for four-syllable input ([2643d64](https://github.com/metasequoiaime/MSIME-Engine/commit/2643d6418b04aad4ab31f54e7f2dcf66d274d89d))
* **quanpin:** enumerate alternative segmentations for four-syllable input ([86c5fa7](https://github.com/metasequoiaime/MSIME-Engine/commit/86c5fa76af8effb24691fffadff80fd29cdd791a))
* **quanpin:** preserve manual segmentation in preedit and candidate cache ([014ca82](https://github.com/metasequoiaime/MSIME-Engine/commit/014ca82b05b36ffe5048c530ec185b27c267f944))
* **quanpin:** respect the autocorrect switch for correction segmentation ([0615c0f](https://github.com/metasequoiaime/MSIME-Engine/commit/0615c0f463615fef66e409525362eeeba7e10e16))
* **quanpin:** respect the autocorrect switch for correction segmentation ([53510e8](https://github.com/metasequoiaime/MSIME-Engine/commit/53510e8a4681960b12d59ddb9be339fb7f1a0c40))
* **quanpin:** support v aliases for umlaut syllables ([6d6215d](https://github.com/metasequoiaime/MSIME-Engine/commit/6d6215d80d12eaa16718823e5201a83e800e404c))
* **quanpin:** support v aliases for umlaut syllables ([d40e107](https://github.com/metasequoiaime/MSIME-Engine/commit/d40e10718cc9c0f26d8f1c3d6b79b1ed5a79be0c))
* **runtime:** reject overlapping resource user and cache roots ([#38](https://github.com/metasequoiaime/MSIME-Engine/issues/38)) ([59b76c0](https://github.com/metasequoiaime/MSIME-Engine/commit/59b76c058a46f305515961f8e2a2a0192a4a9b24))
* **session:** accept Microsoft shuangpin ing through public character input ([#43](https://github.com/metasequoiaime/MSIME-Engine/issues/43)) ([745f4a9](https://github.com/metasequoiaime/MSIME-Engine/commit/745f4a9790ba73268f7671aaf4a0af2ec81e1a67))
* **session:** preserve absent commits when flushing an empty local mode ([ae7c521](https://github.com/metasequoiaime/MSIME-Engine/commit/ae7c521888d23918d92084b36d47fb6feec0b1e3))
* **session:** preserve partial composition across portable frontends ([7cfdb41](https://github.com/metasequoiaime/MSIME-Engine/commit/7cfdb41e7b42803f8b13ef734e3fbb2ce366eb6f))
* **session:** preserve partial selections across portable frontends ([f23d99d](https://github.com/metasequoiaime/MSIME-Engine/commit/f23d99df0da4531e81c11a4b568e757cd231ccad))
* **shuangpin:** drop the stale data-root snapshot and the env-wide test override ([#8](https://github.com/metasequoiaime/MSIME-Engine/issues/8)) ([7c4446f](https://github.com/metasequoiaime/MSIME-Engine/commit/7c4446f8d790761afc89c194c06f5397f1b84ca0))
* **shuangpin:** preserve syllable boundaries when storing suggestions ([7ca8a5d](https://github.com/metasequoiaime/MSIME-Engine/commit/7ca8a5d702511153122e70f3370642510ee6d57e))
* **tests:** add the source files imetest already references ([#7](https://github.com/metasequoiaime/MSIME-Engine/issues/7)) ([49d88a4](https://github.com/metasequoiaime/MSIME-Engine/commit/49d88a4cc68ec5f5ffa3c08ff0dd7f3af22c7c14))
* **translations:** override common greetings mistranslated by the ECDICT reverse lookup ([b674a4c](https://github.com/metasequoiaime/MSIME-Engine/commit/b674a4cf2ee0136073a9dc6580653b7a8a9f098a))
* **translations:** override common greetings mistranslated by the ECDICT reverse lookup ([16d90ec](https://github.com/metasequoiaime/MSIME-Engine/commit/16d90ec1564b61c8f01c0cd5a428c87641df62b8))
* **user-dictionary:** rank single-letter selections against the visible candidate list ([eb07af7](https://github.com/metasequoiaime/MSIME-Engine/commit/eb07af7c3af9939aa0bccb891d069fd9ec73dbb8))
* **user-dictionary:** rank single-letter selections against the visible candidate list ([d8267e7](https://github.com/metasequoiaime/MSIME-Engine/commit/d8267e7bfe4d806bbf7883cfb6bc9392e36c86d4))
* **voice:** retire obsolete standalone build entry points ([703f802](https://github.com/metasequoiaime/MSIME-Engine/commit/703f802dc4d48a73ff4a9b816406128e38b7e8c7))
* **voice:** retire obsolete standalone build entry points ([1f0e076](https://github.com/metasequoiaime/MSIME-Engine/commit/1f0e07632407c30ed632a361b09ec2640779f31e))
* **voice:** tolerate Windows max macros in HTTP transport ([b38d5cb](https://github.com/metasequoiaime/MSIME-Engine/commit/b38d5cb2575be80476c961f48cbee0a7519d6398))
* 修复审计发现的语音帧越界、组词状态残留、双拼切分与英文调频事务缺陷 ([#36](https://github.com/metasequoiaime/MSIME-Engine/issues/36)) ([7d5169d](https://github.com/metasequoiaime/MSIME-Engine/commit/7d5169d0083cd87e6588b6399602755d0aefde2d))


### Performance Improvements

* **pinyin:** remove candidate query limits ([acd6e60](https://github.com/metasequoiaime/MSIME-Engine/commit/acd6e600e8b23d39e30a0fbf57c07709fc527c60))
* reuse persistent database connections for fixed candidates ([e2b0e8d](https://github.com/metasequoiaime/MSIME-Engine/commit/e2b0e8d8016bdfbaccd95536b2b006f7359f1542))
