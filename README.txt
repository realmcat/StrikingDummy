- Caster tax is applied to the end of casts. 

- DoTs and buffs apply immediately.

- Instant actions (OGCDs and instant casts) are assumed to take 0.6s of animation lock + 0.1s of caster tax.

- The simulator cannot wait for arbitrary periods of time.

- Given a state and a set of actions, predicts the expected dps for the next X seconds for each action assuming that all subsequent actions follow a greedy policy.
