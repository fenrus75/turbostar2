# DNN Ideas

overall concept: Build a DNN-style machine learning network
that takes 2 "entries" and votes which one is more important than the other
This allows for a "tournament style" filtering


# entries
Two levels
- whole epic 
- turns within an epic


# Network architecture

- tokenize the input
   tbd on how size of vocabulary

- embedding each token to, say, a 128 vector

Open question: do we need a position embedding multipled/mixed in


per input: consolidate to 4 vectors (4x128) so (2x 4x128 total)
vector 0 gets tokens for position 0, 4, 8, 12, etc added up into it
vector 1 gets 1, 5, 9, 13, ..
...

concat all, then 4 layers of basic fully connected layer with RELU as activation
  (RELU trains poorly, so we may need a RELU variant that has some small non-zero gradient for negative)

then a mapping layer (no activation) to reduce to 2 

then softmax to get to probabilities


# usage

when we have to reduce by some amount, hold a contest where the winner stays


# training data

nice part for the contest model is that we can get N^2 number of training samples

- we have a large LLM available, we ask it the full same question, and use its answer as
the saved goal for the training

