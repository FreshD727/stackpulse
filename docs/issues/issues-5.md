# General

Soft drops correctly when holding the button down. However, if you keep holding the button until the next piece spawns it also begins to soft drop that one. I should have to re-press the down button to soft drop the newly spawned piece

UI is way too confusing there is too much information on the screen. make it more minimal

# Multiplayer

Allow level selection from player 1 for classic and fusion

The windows showing the next pieces is on the left now which is different from solo and other tetris games where it is always on the right. Implement this such that the next piece window is directly to the right of the playspace and then the views of the other players should be even further to the right of the next piece window.

The room system is pretty bad rn. Start the screen with a screen where the user either accepts the pre-generated name or they can choose their own name if they please, afterwards either a create lobby or join lobby button. Pressing the create lobby button moves to the configuration screen of the lobby with name, password, max user count, game mode etc. Pressing the join lobby button should list all created lobbies on the server to choose from, select a lobby and confirm will ask for the password if there is one and otherwise immediately join.

The client executable should not need a specified game mode to start, instead the server should allow lobbies with any game mode to be created.

Also the join room button would sometimes join as a spectator instead of a player in various server states.

Readying up should be possible with a controller using the start button.

The left side of the screen should have a leaderboard where each player is listed with their score, the leaderboard should be sorted by highest scores.

A visual indication for a player that has topped out and a different indication if they disconnected