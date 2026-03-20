==============================
Spread Neighborhood (SpreadRad)
==============================

Cell2Fire now supports configurable spread neighborhoods through ``--SpreadRad``.

Background
----------

The original spread model uses a 1-neighborhood: each burning cell can spread to its
immediate Moore neighbors (up to 8 cells in the interior of the grid).

With ``--SpreadRad n``, the candidate spread neighborhood is extended to the Moore
radius ``n`` around the burning cell:

- ``n=1`` gives up to 8 neighbors
- ``n=2`` gives up to 24 neighbors
- in general, interior cells have ``(2n+1)^2 - 1`` potential neighbors

Modeling Assumption Retained
----------------------------

The spread logic keeps the existing modeling assumption: fire progress from source cell
``i`` to all candidate neighbors uses the ROS determined at source cell ``i``.
Target neighbors do not recompute ROS for this incoming message.

Usage
-----

Default behavior (backward compatible):

.. code-block:: bash

   python main.py --input-instance-folder ../data/Sub40x40/ --output-folder ../results/Sub40x40

Explicit 1-neighborhood (same as default):

.. code-block:: bash

   python main.py --input-instance-folder ../data/Sub40x40/ --output-folder ../results/Sub40x40 --SpreadRad 1

Two-tier neighborhood:

.. code-block:: bash

   python main.py --input-instance-folder ../data/Sub40x40/ --output-folder ../results/Sub40x40 --SpreadRad 2

Notes
-----

- ``--SpreadRad`` only affects fire spread neighborhood geometry.
- ``--IgnitionRad`` is a different parameter and controls ignition-area expansion.
